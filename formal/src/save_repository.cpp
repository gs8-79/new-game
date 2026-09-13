#include "tribe/save_repository.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace tribe {
namespace {

// 本匿名命名空间的二进制读写、校验和与恢复辅助函数仅操作传入缓冲区、路径或候选状态。
// 读取失败必须在写入候选状态前返回 false；文件替换必须经临时档和回滚副本完成，不能直接覆盖唯一主档。
constexpr std::array<char, 8> kMagic{{'T', 'R', 'I', 'B', 'E', 'S', 'A', 'V'}};
constexpr std::size_t kMaximumSaveBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaximumStringBytes = 1024U * 1024U;
constexpr std::size_t kMaximumProfileFactions = 3U;
constexpr std::size_t kMaximumRoster = 64U;
constexpr std::size_t kMaximumSquads = 8U;
constexpr std::size_t kMaximumSquadMembers = 8U;
constexpr std::size_t kMaximumInventoryItems = 64U;
constexpr std::size_t kMaximumLeadershipEntries = 256U;
constexpr std::size_t kMaximumChronicleEntries = 200U;

/// 用途：按固定小端格式累积存档字节。输入：各写入函数的字段值。输出：缓冲区或有效性。
/// 状态影响：仅修改本对象缓冲区。失败：超出长度上限时置为无效；不变量：写入顺序必须与 BufferReader 对称。
class BufferWriter {
   public:
    /// 用途：追加一个原始字节。输入：8 位值。输出：无。
    /// 状态影响：增长 data_。失败：无即时失败；不变量：字节不进行字符编码转换。
    void writeByte(const std::uint8_t value) { data_.push_back(static_cast<char>(value)); }

    /// 用途：以小端序写入无符号 32 位整数。输入：数值。输出：无。
    /// 状态影响：追加四字节。失败：由 valid() 统一报告大小超限；不变量：与 readU32 完全对称。
    void writeU32(const std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            writeByte(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
        }
    }

    /// 用途：以二进制位模式写入 32 位有符号整数。输入：int。输出：无。
    /// 状态影响：追加四字节。失败：非 32 位 int 在编译期拒绝；不变量：负数位模式不可被数值转换破坏。
    void writeInt(const int value) {
        static_assert(sizeof(int) == sizeof(std::int32_t), "Campaign saves require 32-bit int fields.");
        const auto signedValue = static_cast<std::int32_t>(value);
        std::uint32_t bits = 0;
        std::memcpy(&bits, &signedValue, sizeof(bits));
        writeU32(bits);
    }

    /// 用途：将布尔值规范化为 0 或 1。输入：布尔值。输出：无。
    /// 状态影响：追加一字节。失败：无；不变量：读取端只接受这两个编码。
    void writeBool(const bool value) { writeByte(value ? 1U : 0U); }

    /// 用途：追加已知长度的原始字段。输入：字节指针和长度。输出：无。
    /// 状态影响：增长 data_。失败：调用方保证指针在长度内有效；不变量：不插入终止符或长度字段。
    void writeRaw(const char* data, const std::size_t size) { data_.append(data, size); }

    /// 用途：写入带 32 位长度前缀的字符串。输入：文本视图。输出：无。
    /// 状态影响：追加长度与原始字节。失败：长度超限则置 valid_ 为 false；不变量：不修改 UTF-8 字节内容。
    void writeString(const std::string_view value) {
        if (value.size() > kMaximumStringBytes ||
            value.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            valid_ = false;
            return;
        }
        writeU32(static_cast<std::uint32_t>(value.size()));
        writeRaw(value.data(), value.size());
    }

    /// 用途：报告缓冲区是否仍可作为合法存档。输入：无。输出：布尔值；无状态修改。
    /// 失败：字段或总大小超限返回 false。不变量：false 不会自动截断已写入数据。
    bool valid() const { return valid_ && data_.size() <= kMaximumSaveBytes; }
    /// 用途：只读查看当前字节。输入：无。输出：常量引用；无状态修改。
    /// 失败：无。不变量：调用方不得借此修改序列化缓冲区。
    const std::string& data() const { return data_; }
    /// 用途：转移已累积字节。输入：无。输出：字符串。
    /// 状态影响：将 data_ 移出。失败：无；不变量：调用者须在转移前用 valid() 确认大小。
    std::string take() { return std::move(data_); }

   private:
    std::string data_;
    bool valid_ = true;
};

/// 用途：从受限字节视图按固定小端格式读取字段。输入：完整文件或载荷视图。输出：字段值及成功标志。
/// 状态影响：仅推进读取偏移。失败：截断、非法布尔或长度越界返回 false；不变量：失败不得越过输入边界。
class BufferReader {
   public:
    /// 用途：绑定待解析字节。输入：不可变字节视图。输出：读取器。
    /// 状态影响：初始化 offset_ 为零。失败：无；不变量：调用者在解析期间保持视图有效。
    explicit BufferReader(const std::string_view data) : data_(data) {}

    /// 用途：读取一个原始字节。输入：输出引用。输出：是否成功。
    /// 状态影响：成功时推进一字节。失败：到达末尾不修改输出；不变量：offset_ 不超过 data_ 长度。
    bool readByte(std::uint8_t& value) {
        if (offset_ >= data_.size()) return false;
        value = static_cast<std::uint8_t>(static_cast<unsigned char>(data_[offset_++]));
        return true;
    }

    /// 用途：按小端序读取无符号 32 位整数。输入：输出引用。输出：是否成功。
    /// 状态影响：成功时推进四字节。失败：截断返回 false；不变量：失败不会将越界字节计入数值。
    bool readU32(std::uint32_t& value) {
        value = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            std::uint8_t byte = 0;
            if (!readByte(byte)) return false;
            value |= static_cast<std::uint32_t>(byte) << shift;
        }
        return true;
    }

    /// 用途：读取保存的 32 位有符号整数位模式。输入：输出引用。输出：是否成功。
    /// 状态影响：成功时推进四字节。失败：截断或非 32 位 int 时拒绝；不变量：与 writeInt 位级对称。
    bool readInt(int& value) {
        static_assert(sizeof(int) == sizeof(std::int32_t), "Campaign saves require 32-bit int fields.");
        std::uint32_t bits = 0;
        if (!readU32(bits)) return false;
        std::int32_t signedValue = 0;
        std::memcpy(&signedValue, &bits, sizeof(bits));
        value = static_cast<int>(signedValue);
        return true;
    }

    /// 用途：读取规范布尔字段。输入：输出引用。输出：是否成功。
    /// 状态影响：成功时推进一字节。失败：非 0/1 编码或截断返回 false；不变量：不容忍歧义布尔值。
    bool readBool(bool& value) {
        std::uint8_t raw = 0;
        if (!readByte(raw) || raw > 1U) return false;
        value = raw != 0U;
        return true;
    }

    /// 用途：切出连续原始字节。输入：请求长度和输出视图。输出：是否成功。
    /// 状态影响：成功时推进 size。失败：剩余字节不足时返回 false；不变量：返回视图始终位于 data_ 内。
    bool readBytes(const std::size_t size, std::string_view& value) {
        if (size > data_.size() - offset_) return false;
        value = data_.substr(offset_, size);
        offset_ += size;
        return true;
    }

    /// 用途：读取带长度前缀的字符串。输入：输出字符串。输出：是否成功。
    /// 状态影响：成功时推进长度和内容。失败：长度超限或截断返回 false；不变量：不解释或修复原始文本。
    bool readString(std::string& value) {
        std::uint32_t size = 0;
        if (!readU32(size) || size > kMaximumStringBytes) return false;
        std::string_view bytes;
        if (!readBytes(size, bytes)) return false;
        value.assign(bytes.data(), bytes.size());
        return true;
    }

    /// 用途：检查是否恰好消费完整输入。输入：无。输出：布尔值；无状态修改。
    /// 失败：剩余尾随字节返回 false。不变量：完整载荷不得含未定义尾随数据。
    bool finished() const { return offset_ == data_.size(); }

   private:
    std::string_view data_;
    std::size_t offset_ = 0U;
};

/// 用途：计算载荷的 FNV-1a 校验和。输入：原始载荷。输出：32 位校验值；无状态修改。
/// 失败：无。不变量：同一字节序列必产生同一结果，且不依赖平台字符有符号性。
std::uint32_t checksum(const std::string_view data) {
    std::uint32_t value = 2166136261U;
    for (const unsigned char byte : data) {
        value ^= byte;
        value *= 16777619U;
    }
    return value;
}

/// 用途：将枚举按 int 编码写入。输入：写入器和枚举。输出：无。
/// 状态影响：仅修改 writer。失败：枚举合法性由调用方保证；不变量：读取端必须使用相同枚举范围。
template <typename Enum>
void writeEnum(BufferWriter& writer, const Enum value) {
    writer.writeInt(static_cast<int>(value));
}

/// 用途：读取并校验枚举范围。输入：读取器、输出枚举及闭区间。输出：是否成功。
/// 状态影响：仅推进 reader。失败：截断或越界返回 false；不变量：失败不把非法整数转换为枚举。
template <typename Enum>
bool readEnum(BufferReader& reader, Enum& value, const Enum minimum, const Enum maximum) {
    int raw = 0;
    if (!reader.readInt(raw) || raw < static_cast<int>(minimum) || raw > static_cast<int>(maximum)) {
        return false;
    }
    value = static_cast<Enum>(raw);
    return true;
}

/// 用途：顺序写入固定长度布尔数组。输入：写入器和数组。输出：无。
/// 状态影响：仅修改 writer。失败：无；不变量：元素数由模板参数固定。
template <std::size_t Size>
void writeBoolArray(BufferWriter& writer, const std::array<bool, Size>& values) {
    for (const bool value : values) writer.writeBool(value);
}

/// 用途：顺序读取固定长度布尔数组。输入：读取器和输出数组。输出：是否成功。
/// 状态影响：成功时修改数组并推进 reader。失败：截断或非法布尔返回 false；不变量：不读取超出 Size 的元素。
template <std::size_t Size>
bool readBoolArray(BufferReader& reader, std::array<bool, Size>& values) {
    for (std::size_t index = 0; index < Size; ++index) {
        bool value = false;
        if (!reader.readBool(value)) return false;
        values[index] = value;
    }
    return true;
}

/// 用途：写入角色属性数组。输入：写入器和属性。输出：无。
/// 状态影响：仅修改 writer。失败：无；不变量：属性顺序与 Attributes::values 一致。
void writeAttributes(BufferWriter& writer, const Attributes& attributes) {
    for (const int value : attributes.values) writer.writeInt(value);
}

/// 用途：读取角色属性数组。输入：读取器和输出属性。输出：是否成功。
/// 状态影响：成功时填充 attributes。失败：截断返回 false；不变量：数值范围由完整状态校验统一判断。
bool readAttributes(BufferReader& reader, Attributes& attributes) {
    for (int& value : attributes.values) {
        if (!reader.readInt(value)) return false;
    }
    return true;
}

/// 用途：序列化一个物品。输入：写入器和物品。输出：无。
/// 状态影响：仅修改 writer。失败：字段合法性由状态校验保证；不变量：字段顺序与 readItem 对称。
void writeItem(BufferWriter& writer, const Item& item) {
    writer.writeString(item.id);
    writer.writeString(item.name);
    writeEnum(writer, item.quality);
    writeEnum(writer, item.condition);
    writer.writeInt(item.weight);
    writer.writeInt(item.slotCount);
    writer.writeBool(item.equipmentSlot.has_value());
    if (item.equipmentSlot) writeEnum(writer, *item.equipmentSlot);
    writeAttributes(writer, item.bonuses);
}

/// 用途：反序列化并做局部物品校验。输入：读取器和输出物品。输出：是否成功。
/// 状态影响：成功时修改 item。失败：非法枚举、空标识或容量字段返回 false；不变量：槽位索引与装备槽一致。
bool readItem(BufferReader& reader, Item& item) {
    bool hasSlot = false;
    if (!reader.readString(item.id) || !reader.readString(item.name) ||
        !readEnum(reader, item.quality, ItemQuality::Crude, ItemQuality::Legendary) ||
        !readEnum(reader, item.condition, ItemCondition::Intact, ItemCondition::Scrapped) ||
        !reader.readInt(item.weight) || !reader.readInt(item.slotCount) || !reader.readBool(hasSlot)) {
        return false;
    }
    if (hasSlot) {
        EquipmentSlot slot = EquipmentSlot::MainHand;
        if (!readEnum(reader, slot, EquipmentSlot::MainHand, EquipmentSlot::Accessory)) return false;
        item.equipmentSlot = slot;
    } else {
        item.equipmentSlot.reset();
    }
    if (!readAttributes(reader, item.bonuses)) return false;
    return !item.id.empty() && !item.name.empty() && item.weight >= 0 && item.slotCount > 0;
}

/// 用途：序列化角色及其装备。输入：写入器和角色。输出：无。
/// 状态影响：仅修改 writer。失败：无；不变量：每个装备槽都保留存在标志。
void writeCharacter(BufferWriter& writer, const Character& character) {
    writer.writeString(character.name);
    writeEnum(writer, character.occupation);
    writer.writeInt(character.level);
    writer.writeInt(character.experience);
    writer.writeInt(character.growthPoints);
    writer.writeInt(character.life);
    writer.writeInt(character.fatigue);
    writer.writeInt(character.loyalty);
    writeAttributes(writer, character.attributes);
    for (const auto& equipped : character.equipment) {
        writer.writeBool(equipped.has_value());
        if (equipped) writeItem(writer, *equipped);
    }
}

/// 用途：反序列化角色与装备。输入：读取器和输出角色。输出：是否成功。
/// 状态影响：成功时修改 character。失败：字段截断、数值越界或槽位错配返回 false；不变量：装备只落在声明槽位。
bool readCharacter(BufferReader& reader, Character& character) {
    if (!reader.readString(character.name) ||
        !readEnum(reader, character.occupation, Occupation::Hunter, Occupation::Envoy) ||
        !reader.readInt(character.level) || !reader.readInt(character.experience) ||
        !reader.readInt(character.growthPoints) || !reader.readInt(character.life) ||
        !reader.readInt(character.fatigue) || !reader.readInt(character.loyalty) ||
        !readAttributes(reader, character.attributes)) {
        return false;
    }
    if (character.name.empty() || character.level <= 0 || character.level > 1000000 || character.experience < 0 ||
        character.growthPoints < 0) {
        return false;
    }
    for (const int value : character.attributes.values) {
        if (value < kMinimumAttribute || value > kMaximumAttribute) return false;
    }
    for (std::size_t index = 0; index < character.equipment.size(); ++index) {
        bool hasItem = false;
        if (!reader.readBool(hasItem)) return false;
        if (!hasItem) {
            character.equipment[index].reset();
            continue;
        }
        Item item;
        if (!readItem(reader, item) || !item.equipmentSlot || static_cast<std::size_t>(*item.equipmentSlot) != index) {
            return false;
        }
        character.equipment[index] = std::move(item);
    }
    return true;
}

/// 用途：序列化任务背包。输入：写入器和背包。输出：无。
/// 状态影响：仅修改 writer。失败：无；不变量：物品计数与后续物品序列一致。
void writeInventory(BufferWriter& writer, const Inventory& inventory) {
    writer.writeInt(inventory.weightLimit());
    writer.writeInt(inventory.slotLimit());
    writer.writeU32(static_cast<std::uint32_t>(inventory.items().size()));
    for (const Item& item : inventory.items()) writeItem(writer, item);
}

/// 用途：反序列化受容量约束的任务背包。输入：读取器和输出背包。输出：是否成功。
/// 状态影响：仅成功时替换 inventory。失败：限额、数量、物品或装入失败返回 false；不变量：读取失败不提交部分背包。
bool readInventory(BufferReader& reader, Inventory& inventory) {
    int weightLimit = 0;
    int slotLimit = 0;
    std::uint32_t count = 0;
    if (!reader.readInt(weightLimit) || !reader.readInt(slotLimit) || !reader.readU32(count) || weightLimit < 0 ||
        slotLimit < 0 || count > kMaximumInventoryItems) {
        return false;
    }
    Inventory parsed{weightLimit, slotLimit};
    for (std::uint32_t index = 0; index < count; ++index) {
        Item item;
        if (!readItem(reader, item) || !parsed.pickupFree(std::move(item))) return false;
    }
    inventory = std::move(parsed);
    return true;
}

/// 用途：序列化活动任务小队。输入：写入器和小队。输出：无。
/// 状态影响：仅修改 writer。失败：无；不变量：成员顺序和 leaderIndex 原样保存。
void writeExpansionSquad(BufferWriter& writer, const Squad& squad) {
    writer.writeString(squad.name);
    writer.writeU32(static_cast<std::uint32_t>(squad.members.size()));
    for (const Character& member : squad.members) writeCharacter(writer, member);
    writer.writeU32(static_cast<std::uint32_t>(squad.leaderIndex));
    writer.writeInt(squad.cohesion);
}

/// 用途：反序列化活动任务小队。输入：读取器和输出小队。输出：是否成功。
/// 状态影响：成功时更新 squad。失败：成员数、队长下标或字段损坏返回 false；不变量：leaderIndex 始终指向成员。
bool readExpansionSquad(BufferReader& reader, Squad& squad) {
    std::uint32_t count = 0;
    std::uint32_t leaderIndex = 0;
    if (!reader.readString(squad.name) || !reader.readU32(count) || count < kMinimumSquadSize ||
        count > kMaximumSquadSize) {
        return false;
    }
    squad.members.clear();
    squad.members.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        Character character;
        if (!readCharacter(reader, character)) return false;
        squad.members.push_back(std::move(character));
    }
    if (!reader.readU32(leaderIndex) || leaderIndex >= count || !reader.readInt(squad.cohesion)) {
        return false;
    }
    squad.leaderIndex = leaderIndex;
    return true;
}

/// 用途：序列化活动地图任务状态。输入：写入器和任务状态。输出：无。
/// 状态影响：仅修改 writer。失败：无；不变量：遭遇、地点和货物字段完整保留。
void writeExpansionState(BufferWriter& writer, const ExpansionState& state) {
    writer.writeU32(state.seed);
    writer.writeInt(state.turn);
    writeEnum(writer, state.phase);
    writeExpansionSquad(writer, state.squad);
    writeInventory(writer, state.backpack);
    writer.writeBool(state.settled);
    writer.writeInt(state.worldLocation);
    writeBoolArray(writer, state.worldDiscovered);
    writeBoolArray(writer, state.outposts);
    writer.writeInt(state.cargoFood);
    writer.writeInt(state.cargoWood);
    writer.writeInt(state.cargoStone);
    writer.writeInt(state.cargoHerbs);
    writer.writeInt(state.cargoHides);
    writer.writeInt(state.harvestActions);
    writer.writeInt(state.cargoCapacity);
    writer.writeInt(state.foodGatherBonus);
    writer.writeInt(state.herbGatherBonus);
    writeEnum(writer, state.missionKind);
    writeEnum(writer, state.assignedResource);
    writer.writeInt(state.crewSize);
    writer.writeInt(state.encounterLife);
    writer.writeBool(state.encounterDefeated);
}

/// 用途：反序列化并验证活动地图任务。输入：读取器和输出状态。输出：是否成功。
/// 状态影响：成功时修改 state。失败：字段损坏或 ExpansionGame 校验失败返回 false；不变量：岩牙遭遇互斥条件必须成立。
bool readExpansionState(BufferReader& reader, ExpansionState& state) {
    if (!reader.readU32(state.seed) || !reader.readInt(state.turn) ||
        !readEnum(reader, state.phase, ExpansionPhase::Exploring, ExpansionPhase::Settled) ||
        !readExpansionSquad(reader, state.squad) || !readInventory(reader, state.backpack) ||
        !reader.readBool(state.settled) || !reader.readInt(state.worldLocation) ||
        !readBoolArray(reader, state.worldDiscovered) || !readBoolArray(reader, state.outposts) ||
        !reader.readInt(state.cargoFood) || !reader.readInt(state.cargoWood) || !reader.readInt(state.cargoStone) ||
        !reader.readInt(state.cargoHerbs) || !reader.readInt(state.cargoHides) ||
        !reader.readInt(state.harvestActions) || !reader.readInt(state.cargoCapacity) ||
        !reader.readInt(state.foodGatherBonus) || !reader.readInt(state.herbGatherBonus) ||
        !readEnum(reader, state.missionKind, MissionKind::Gather, MissionKind::OutpostConstruction) ||
        !readEnum(reader, state.assignedResource, ResourceKind::Food, ResourceKind::Hides) ||
        !reader.readInt(state.crewSize) || !reader.readInt(state.encounterLife) ||
        !reader.readBool(state.encounterDefeated)) {
        return false;
    }
    return static_cast<bool>(ExpansionGame::validateState(state));
}

/// 用途：序列化派系状态。输入：写入器和派系。输出：无。
/// 状态影响：仅修改 writer。失败：无；不变量：危机枚举与 readFaction 范围一致。
void writeFaction(BufferWriter& writer, const FactionState& faction) {
    writer.writeString(faction.name);
    writer.writeInt(faction.influence);
    writer.writeInt(faction.satisfaction);
    writer.writeString(faction.demand);
    writer.writeString(faction.candidate);
    writeEnum(writer, faction.crisis);
}

/// 用途：反序列化派系状态。输入：读取器和输出派系。输出：是否成功。
/// 状态影响：成功时修改 faction。失败：字段截断或危机越界返回 false；不变量：完整文本在最终状态校验中复核。
bool readFaction(BufferReader& reader, FactionState& faction) {
    return reader.readString(faction.name) && reader.readInt(faction.influence) &&
           reader.readInt(faction.satisfaction) && reader.readString(faction.demand) &&
           reader.readString(faction.candidate) &&
           readEnum(reader, faction.crisis, FactionCrisis::Calm, FactionCrisis::Coup);
}

/// 用途：序列化部落档案及内部派系。输入：写入器和档案。输出：无。
/// 状态影响：仅修改 writer。失败：无；不变量：派系数量紧随档案字段。
void writeTribeProfile(BufferWriter& writer, const TribeProfile& profile) {
    writeEnum(writer, profile.id);
    writer.writeString(profile.name);
    writer.writeString(profile.leader);
    writer.writeString(profile.actingLeader);
    writer.writeString(profile.successor);
    writer.writeString(profile.personality);
    writer.writeU32(static_cast<std::uint32_t>(profile.factions.size()));
    for (const FactionState& faction : profile.factions) writeFaction(writer, faction);
}

/// 用途：反序列化部落档案。输入：读取器和输出档案。输出：是否成功。
/// 状态影响：成功时更新 profile。失败：部落、派系数量或字段无效返回 false；不变量：派系数处于受限区间。
bool readTribeProfile(BufferReader& reader, TribeProfile& profile) {
    std::uint32_t count = 0;
    if (!readEnum(reader, profile.id, TribeId::Player, TribeId::Blackstone) || !reader.readString(profile.name) ||
        !reader.readString(profile.leader) || !reader.readString(profile.actingLeader) ||
        !reader.readString(profile.successor) || !reader.readString(profile.personality) || !reader.readU32(count) ||
        count < 2U || count > kMaximumProfileFactions) {
        return false;
    }
    profile.factions.clear();
    profile.factions.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        FactionState faction;
        if (!readFaction(reader, faction)) return false;
        profile.factions.push_back(std::move(faction));
    }
    return true;
}

/// 用途：序列化双边外交关系。输入：写入器和关系。输出：无。
/// 状态影响：仅修改 writer。失败：无；不变量：所有关系开关按固定顺序写入。
void writeRelation(BufferWriter& writer, const DiplomacyRelation& relation) {
    writer.writeInt(relation.relation);
    writer.writeInt(relation.trust);
    writer.writeInt(relation.fear);
    writer.writeInt(relation.tradeDependence);
    writer.writeBool(relation.atWar);
    writer.writeBool(relation.truce);
    writer.writeBool(relation.alliance);
    writer.writeBool(relation.marriage);
    writer.writeBool(relation.playerPaysTribute);
    writer.writeBool(relation.otherPaysTribute);
    writer.writeBool(relation.tradeRoute);
}

/// 用途：反序列化双边外交关系。输入：读取器和输出关系。输出：是否成功。
/// 状态影响：成功时修改 relation。失败：字段截断或布尔编码异常返回 false；不变量：跨关系逻辑由状态校验统一复核。
bool readRelation(BufferReader& reader, DiplomacyRelation& relation) {
    return reader.readInt(relation.relation) && reader.readInt(relation.trust) && reader.readInt(relation.fear) &&
           reader.readInt(relation.tradeDependence) && reader.readBool(relation.atWar) &&
           reader.readBool(relation.truce) && reader.readBool(relation.alliance) &&
           reader.readBool(relation.marriage) && reader.readBool(relation.playerPaysTribute) &&
           reader.readBool(relation.otherPaysTribute) && reader.readBool(relation.tradeRoute);
}

/// 用途：序列化永久小队。输入：写入器和小队。输出：无。
/// 状态影响：仅修改 writer。失败：无；不变量：成员、疲劳和驻地按固定顺序保存。
void writePermanentSquad(BufferWriter& writer, const PermanentSquad& squad) {
    writer.writeString(squad.name);
    writer.writeString(squad.captain);
    writer.writeU32(static_cast<std::uint32_t>(squad.members.size()));
    for (const std::string& member : squad.members) writer.writeString(member);
    writer.writeInt(squad.fatigue);
    writer.writeInt(squad.eliteExperience);
    writer.writeBool(squad.personallyDeployedThisSeason);
    writer.writeBool(squad.refusingOrders);
    writeEnum(writer, squad.station);
}

/// 用途：反序列化永久小队。输入：读取器和输出小队。输出：是否成功。
/// 状态影响：成功时更新 squad。失败：成员数、驻地枚举或字段无效返回 false；不变量：成员数保持在小队限制内。
bool readPermanentSquad(BufferReader& reader, PermanentSquad& squad) {
    std::uint32_t count = 0;
    if (!reader.readString(squad.name) || !reader.readString(squad.captain) || !reader.readU32(count) || count < 2U ||
        count > kMaximumSquadMembers) {
        return false;
    }
    squad.members.clear();
    squad.members.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        std::string member;
        if (!reader.readString(member)) return false;
        squad.members.push_back(std::move(member));
    }
    return reader.readInt(squad.fatigue) && reader.readInt(squad.eliteExperience) &&
           reader.readBool(squad.personallyDeployedThisSeason) && reader.readBool(squad.refusingOrders) &&
           readEnum(reader, squad.station, WorldLocationId::Camp, WorldLocationId::CliffTradeRoad);
}

/// 用途：序列化战争及锁定装备。输入：写入器和战争状态。输出：无。
/// 状态影响：仅修改 writer。失败：无；不变量：锁定装备只作为战争所有权的一份记录。
void writeWar(BufferWriter& writer, const WarState& war) {
    writer.writeBool(war.active);
    writeEnum(writer, war.enemy);
    writer.writeString(war.commander);
    writer.writeInt(war.warriors);
    writer.writeInt(war.militia);
    writer.writeInt(war.playerPower);
    writer.writeInt(war.enemyPower);
    writeEnum(writer, war.order);
    writer.writeBool(war.riskConfirmed);
    writer.writeInt(war.spearMilitia);
    writer.writeInt(war.shieldBearers);
    writer.writeInt(war.heavySpears);
    writer.writeInt(war.craftsmanshipPower);
    writer.writeU32(static_cast<std::uint32_t>(war.lockedEquipment.size()));
    for (const Item& item : war.lockedEquipment) writeItem(writer, item);
    writer.writeBool(war.defensive);
}

/// 用途：反序列化战争及锁定装备。输入：读取器和输出战争状态。输出：是否成功。
/// 状态影响：成功时更新 war。失败：字段、枚举或装备数量无效返回 false；不变量：最终校验禁止装备双重所有权。
bool readWar(BufferReader& reader, WarState& war) {
    std::uint32_t count = 0;
    if (!reader.readBool(war.active) || !readEnum(reader, war.enemy, TribeId::Player, TribeId::Blackstone) ||
        !reader.readString(war.commander) || !reader.readInt(war.warriors) || !reader.readInt(war.militia) ||
        !reader.readInt(war.playerPower) || !reader.readInt(war.enemyPower) ||
        !readEnum(reader, war.order, WarOrder::Advance, WarOrder::Retreat) || !reader.readBool(war.riskConfirmed) ||
        !reader.readInt(war.spearMilitia) || !reader.readInt(war.shieldBearers) || !reader.readInt(war.heavySpears) ||
        !reader.readInt(war.craftsmanshipPower) || !reader.readU32(count) || count > kMaximumInventoryItems)
        return false;
    war.lockedEquipment.clear();
    for (std::uint32_t i = 0; i < count; ++i) {
        Item item;
        if (!readItem(reader, item)) return false;
        war.lockedEquipment.push_back(std::move(item));
    }
    return reader.readBool(war.defensive);
}

/// 用途：序列化一条编年史。输入：写入器和条目。输出：无。
/// 状态影响：仅修改 writer。失败：无；不变量：季节、重要度与文本顺序固定。
void writeChronicle(BufferWriter& writer, const ChronicleEntry& entry) {
    writer.writeInt(entry.season);
    writer.writeInt(entry.importance);
    writer.writeString(entry.title);
    writer.writeString(entry.detail);
}

/// 用途：反序列化一条编年史。输入：读取器和输出条目。输出：是否成功。
/// 状态影响：成功时修改 entry。失败：字段截断返回 false；不变量：文本安全性由完整状态校验复核。
bool readChronicle(BufferReader& reader, ChronicleEntry& entry) {
    return reader.readInt(entry.season) && reader.readInt(entry.importance) && reader.readString(entry.title) &&
           reader.readString(entry.detail);
}

/// 用途：按 v6 载荷布局序列化完整游戏状态。输入：写入器和合法状态。输出：无。
/// 状态影响：仅修改 writer。失败：调用方须先完成状态校验；不变量：字段顺序不得改动，否则旧存档不可读取。
void writeGameState(BufferWriter& writer, const GameState& state) {
    writeEnum(writer, state.mode);
    writeEnum(writer, state.phase);
    writer.writeU32(state.seed);
    writer.writeInt(state.season);
    writer.writeInt(state.seasonLimit);
    writer.writeInt(state.actionsLeft);
    writer.writeInt(state.population);
    writer.writeInt(state.food);
    writer.writeInt(state.wood);
    writer.writeInt(state.stone);
    writer.writeInt(state.herbs);
    writer.writeInt(state.hides);
    writer.writeInt(state.warriors);
    writer.writeInt(state.morale);
    writer.writeInt(state.campDurability);
    writer.writeInt(state.stability);
    writer.writeInt(state.tradeCount);
    writer.writeInt(state.warsWon);
    writer.writeInt(state.warsLost);
    writer.writeInt(state.missionCount);
    writer.writeInt(state.missionDeaths);
    writer.writeInt(state.highestLevel);
    writer.writeString(state.tribeName);
    writer.writeString(state.leaderName);
    writer.writeString(state.actingLeaderName);
    writer.writeString(state.leaderFocus);
    writeBoolArray(writer, state.discovered);
    writeBoolArray(writer, state.outposts);
    writeBoolArray(writer, state.buildings);
    writeBoolArray(writer, state.technologies);
    for (const TribeProfile& profile : state.tribes) writeTribeProfile(writer, profile);
    for (const DiplomacyRelation& relation : state.relations) writeRelation(writer, relation);
    for (const FactionState& faction : state.playerFactions) writeFaction(writer, faction);
    writeBoolArray(writer, state.tradePartners);

    writer.writeU32(static_cast<std::uint32_t>(state.roster.size()));
    for (const Character& character : state.roster) writeCharacter(writer, character);
    writer.writeU32(static_cast<std::uint32_t>(state.squads.size()));
    for (const PermanentSquad& squad : state.squads) writePermanentSquad(writer, squad);

    writer.writeBool(state.activeMission.has_value());
    if (state.activeMission) writeExpansionState(writer, *state.activeMission);
    writeWar(writer, state.war);
    writer.writeBool(state.longModeFinalShown);
    writeEnum(writer, state.ending);

    writer.writeU32(static_cast<std::uint32_t>(state.leadershipHistory.size()));
    for (const std::string& entry : state.leadershipHistory) writer.writeString(entry);
    writer.writeU32(static_cast<std::uint32_t>(state.chronicle.size()));
    for (const ChronicleEntry& entry : state.chronicle) writeChronicle(writer, entry);

    writer.writeInt(state.workforce.foodCrew);
    writer.writeInt(state.workforce.woodCrew);
    writer.writeInt(state.workforce.stoneCrew);
    writer.writeInt(state.workforce.herbCrew);
    writer.writeInt(state.workforce.crafters);
    writer.writeInt(state.workforce.healers);
    writer.writeInt(state.workforce.scouts);
    writer.writeInt(state.workforce.envoys);
    writer.writeInt(state.workforce.campGuards);
    writer.writeBool(state.pendingEvent.active);
    writeEnum(writer, state.pendingEvent.kind);
    writer.writeBool(state.workforceReassignmentRequired);
    writer.writeString(state.workshopSupervisor);
    writer.writeString(state.healerSupervisor);
    for (const int guard : state.workforce.outpostGuards) writer.writeInt(guard);
    for (const int idle : state.workforce.outpostIdleSeasons) writer.writeInt(idle);
    writer.writeU32(static_cast<std::uint32_t>(state.stockpile.size()));
    for (const Item& item : state.stockpile) writeItem(writer, item);
    for (const OccupationState& site : state.occupations) {
        writer.writeBool(site.occupied);
        writer.writeInt(site.garrison);
        writer.writeInt(site.unrest);
    }
    writer.writeU32(state.nextItemSerial);
}

/// 用途：识别制造装备标识中的全局序号。输入：物品 ID 与输出序号。输出：是否为制造装备。
/// 状态影响：仅成功时写 serial。失败：配方、分隔符或序号非法返回 false；不变量：序号必须为正数。
bool manufacturedItemSerial(const std::string_view id, std::uint32_t& serial) {
    const std::size_t first = id.find('_');
    const std::size_t last = id.rfind('_');
    if (first == std::string_view::npos || first == last || last + 1U >= id.size()) return false;
    const std::string_view recipe = id.substr(0U, first);
    constexpr std::array<std::string_view, 9> recipes{
        {"knife", "spear", "shield", "armor", "shoes", "flintspear", "reinforcedshield", "cloak", "charm"}};
    if (std::find(recipes.begin(), recipes.end(), recipe) == recipes.end()) return false;
    std::uint32_t parsed = 0U;
    const auto result = std::from_chars(id.data() + static_cast<std::ptrdiff_t>(last + 1U),
                                        id.data() + static_cast<std::ptrdiff_t>(id.size()), parsed);
    if (result.ec != std::errc{} || result.ptr != id.data() + static_cast<std::ptrdiff_t>(id.size()) || parsed == 0U)
        return false;
    serial = parsed;
    return true;
}

/// 用途：从 v5 全部装备位置推导下一个全局序号。输入：状态及输出序号、错误。输出：是否成功。
/// 状态影响：仅成功时写 nextSerial。失败：最大序号耗尽返回 false；不变量：结果严格大于所有已出现制造序号。
bool deriveNextItemSerial(const GameState& state, std::uint32_t& nextSerial, std::string& error) {
    std::uint32_t largest = 0U;
    const auto inspect = [&](const Item& item) {
        std::uint32_t serial = 0U;
        if (manufacturedItemSerial(item.id, serial)) largest = std::max(largest, serial);
    };
    for (const Item& item : state.stockpile) inspect(item);
    for (const Item& item : state.war.lockedEquipment) inspect(item);
    for (const Character& character : state.roster)
        for (const auto& item : character.equipment)
            if (item) inspect(*item);
    if (state.activeMission) {
        for (const Item& item : state.activeMission->backpack.items()) inspect(item);
        for (const Character& character : state.activeMission->squad.members)
            for (const auto& item : character.equipment)
                if (item) inspect(*item);
    }
    if (largest == std::numeric_limits<std::uint32_t>::max()) {
        error = "v5 存档的装备编号已耗尽，无法安全升级。";
        return false;
    }
    nextSerial = largest + 1U;
    return true;
}

/// 用途：按 v5/v6 载荷布局解析完整状态并执行集中校验。输入：读取器、输出状态、错误和版本字段标志。输出：是否成功。
/// 状态影响：成功时填充 state。失败：任何字段或不变量无效即返回 false；不变量：未通过 validateState
/// 的状态绝不交给调用方。
bool readGameState(BufferReader& reader, GameState& state, std::string& error, const bool containsItemSerial) {
    if (!readEnum(reader, state.mode, GameMode::Quick, GameMode::Long) ||
        !readEnum(reader, state.phase, GamePhase::Managing, GamePhase::Sandbox) || !reader.readU32(state.seed) ||
        !reader.readInt(state.season) || !reader.readInt(state.seasonLimit) || !reader.readInt(state.actionsLeft) ||
        !reader.readInt(state.population) || !reader.readInt(state.food) || !reader.readInt(state.wood) ||
        !reader.readInt(state.stone) || !reader.readInt(state.herbs) || !reader.readInt(state.hides) ||
        !reader.readInt(state.warriors) || !reader.readInt(state.morale) || !reader.readInt(state.campDurability) ||
        !reader.readInt(state.stability) || !reader.readInt(state.tradeCount) || !reader.readInt(state.warsWon) ||
        !reader.readInt(state.warsLost) || !reader.readInt(state.missionCount) ||
        !reader.readInt(state.missionDeaths) || !reader.readInt(state.highestLevel) ||
        !reader.readString(state.tribeName) || !reader.readString(state.leaderName) ||
        !reader.readString(state.actingLeaderName) || !reader.readString(state.leaderFocus) ||
        !readBoolArray(reader, state.discovered) || !readBoolArray(reader, state.outposts) ||
        !readBoolArray(reader, state.buildings) || !readBoolArray(reader, state.technologies)) {
        error = "存档基础字段损坏或不完整。";
        return false;
    }
    for (TribeProfile& profile : state.tribes) {
        if (!readTribeProfile(reader, profile)) {
            error = "存档的部落档案损坏。";
            return false;
        }
    }
    for (DiplomacyRelation& relation : state.relations) {
        if (!readRelation(reader, relation)) {
            error = "存档的外交关系损坏。";
            return false;
        }
    }
    for (FactionState& faction : state.playerFactions) {
        if (!readFaction(reader, faction)) {
            error = "存档的玩家派系损坏。";
            return false;
        }
    }
    if (!readBoolArray(reader, state.tradePartners)) {
        error = "存档的贸易伙伴字段损坏。";
        return false;
    }

    std::uint32_t rosterCount = 0;
    if (!reader.readU32(rosterCount) || rosterCount < 2U || rosterCount > kMaximumRoster) {
        error = "存档的角色数量无效。";
        return false;
    }
    state.roster.clear();
    state.roster.reserve(rosterCount);
    for (std::uint32_t index = 0; index < rosterCount; ++index) {
        Character character;
        if (!readCharacter(reader, character)) {
            error = "存档的角色字段损坏。";
            return false;
        }
        state.roster.push_back(std::move(character));
    }

    std::uint32_t squadCount = 0;
    if (!reader.readU32(squadCount) || squadCount == 0U || squadCount > kMaximumSquads) {
        error = "存档的永久小队数量无效。";
        return false;
    }
    state.squads.clear();
    state.squads.reserve(squadCount);
    for (std::uint32_t index = 0; index < squadCount; ++index) {
        PermanentSquad squad;
        if (!readPermanentSquad(reader, squad)) {
            error = "存档的永久小队字段损坏。";
            return false;
        }
        state.squads.push_back(std::move(squad));
    }

    bool hasMission = false;
    if (!reader.readBool(hasMission)) {
        error = "存档的任务标志损坏。";
        return false;
    }
    if (hasMission) {
        ExpansionState mission;
        if (!readExpansionState(reader, mission)) {
            error = "存档的活动任务损坏。";
            return false;
        }
        state.activeMission = std::move(mission);
    } else {
        state.activeMission.reset();
    }
    if (!readWar(reader, state.war) || !reader.readBool(state.longModeFinalShown) ||
        !readEnum(reader, state.ending, GameEnding::None, GameEnding::Extinction)) {
        error = "存档的任务、战争或结局字段损坏。";
        return false;
    }

    std::uint32_t leadershipCount = 0;
    if (!reader.readU32(leadershipCount) || leadershipCount == 0U || leadershipCount > kMaximumLeadershipEntries) {
        error = "存档的首领历史数量无效。";
        return false;
    }
    state.leadershipHistory.clear();
    state.leadershipHistory.reserve(leadershipCount);
    for (std::uint32_t index = 0; index < leadershipCount; ++index) {
        std::string entry;
        if (!reader.readString(entry)) {
            error = "存档的首领历史损坏。";
            return false;
        }
        state.leadershipHistory.push_back(std::move(entry));
    }

    std::uint32_t chronicleCount = 0;
    if (!reader.readU32(chronicleCount) || chronicleCount == 0U || chronicleCount > kMaximumChronicleEntries) {
        error = "存档的编年史数量无效。";
        return false;
    }
    state.chronicle.clear();
    state.chronicle.reserve(chronicleCount);
    for (std::uint32_t index = 0; index < chronicleCount; ++index) {
        ChronicleEntry entry;
        if (!readChronicle(reader, entry)) {
            error = "存档的编年史字段损坏。";
            return false;
        }
        state.chronicle.push_back(std::move(entry));
    }

    if (!reader.readInt(state.workforce.foodCrew) || !reader.readInt(state.workforce.woodCrew) ||
        !reader.readInt(state.workforce.stoneCrew) || !reader.readInt(state.workforce.herbCrew) ||
        !reader.readInt(state.workforce.crafters) || !reader.readInt(state.workforce.healers) ||
        !reader.readInt(state.workforce.scouts) || !reader.readInt(state.workforce.envoys) ||
        !reader.readInt(state.workforce.campGuards) || !reader.readBool(state.pendingEvent.active) ||
        !readEnum(reader, state.pendingEvent.kind, PendingEventKind::Refugees, PendingEventKind::FactionDemand) ||
        !reader.readBool(state.workforceReassignmentRequired) || !reader.readString(state.workshopSupervisor) ||
        !reader.readString(state.healerSupervisor)) {
        error = "存档的当前玩法字段损坏。";
        return false;
    }
    for (int& guard : state.workforce.outpostGuards)
        if (!reader.readInt(guard)) {
            error = "存档前哨守卫字段损坏。";
            return false;
        }
    for (int& idle : state.workforce.outpostIdleSeasons)
        if (!reader.readInt(idle)) {
            error = "存档前哨维护字段损坏。";
            return false;
        }
    std::uint32_t stockCount = 0;
    if (!reader.readU32(stockCount) || stockCount > kMaximumInventoryItems) {
        error = "存档仓库数量无效。";
        return false;
    }
    state.stockpile.clear();
    for (std::uint32_t i = 0; i < stockCount; ++i) {
        Item item;
        if (!readItem(reader, item)) {
            error = "存档仓库物品损坏。";
            return false;
        }
        state.stockpile.push_back(std::move(item));
    }
    for (OccupationState& site : state.occupations)
        if (!reader.readBool(site.occupied) || !reader.readInt(site.garrison) || !reader.readInt(site.unrest)) {
            error = "存档占领字段损坏。";
            return false;
        }
    if (containsItemSerial) {
        if (!reader.readU32(state.nextItemSerial) || state.nextItemSerial == 0U) {
            error = "存档的装备序号字段损坏。";
            return false;
        }
    } else if (!deriveNextItemSerial(state, state.nextItemSerial, error)) {
        return false;
    }
    if (!GameEngine::validateState(state, error)) return false;
    error.clear();
    return true;
}

/// 用途：把合法状态封装为带魔数、版本和校验和的 v6 文件。输入：状态及输出字节、错误。输出：是否成功。
/// 状态影响：仅成功时写 fileData。失败：载荷超限返回 false；不变量：文件校验和覆盖完整载荷。
bool serializeFile(const GameState& state, std::string& fileData, std::string& error) {
    BufferWriter payloadWriter;
    writeGameState(payloadWriter, state);
    if (!payloadWriter.valid() ||
        payloadWriter.data().size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        error = "存档内容过大，无法安全写入。";
        return false;
    }
    const std::string payload = payloadWriter.data();
    BufferWriter fileWriter;
    fileWriter.writeRaw(kMagic.data(), kMagic.size());
    fileWriter.writeU32(static_cast<std::uint32_t>(kSaveVersion));
    fileWriter.writeU32(static_cast<std::uint32_t>(payload.size()));
    fileWriter.writeU32(checksum(payload));
    fileWriter.writeRaw(payload.data(), payload.size());
    if (!fileWriter.valid()) {
        error = "存档内容过大，无法安全写入。";
        return false;
    }
    fileData = fileWriter.take();
    error.clear();
    return true;
}

/// 用途：校验文件头、校验和和载荷后得到候选状态。输入：文件字节、候选状态、错误及可选版本。输出：是否成功。
/// 状态影响：仅成功时写 candidate/sourceVersion。失败：不支持版本或任一校验失败返回 false；不变量：拒绝尾随未定义字节。
bool deserializeFile(const std::string_view fileData, GameState& candidate, std::string& error,
                     std::uint32_t* sourceVersion = nullptr) {
    BufferReader fileReader(fileData);
    std::string_view magic;
    std::uint32_t version = 0;
    std::uint32_t payloadSize = 0;
    std::uint32_t storedChecksum = 0;
    if (!fileReader.readBytes(kMagic.size(), magic) ||
        !std::equal(kMagic.begin(), kMagic.end(), magic.begin(), magic.end())) {
        error = "不是《燧火纪》游戏存档。";
        return false;
    }
    if (!fileReader.readU32(version) || (version != static_cast<std::uint32_t>(kSaveVersion) && version != 5U)) {
        error = "旧版本存档不支持，需要新开局；原文件未被修改。";
        return false;
    }
    if (!fileReader.readU32(payloadSize) || payloadSize > kMaximumSaveBytes || !fileReader.readU32(storedChecksum)) {
        error = "存档头损坏。";
        return false;
    }
    std::string_view payload;
    if (!fileReader.readBytes(payloadSize, payload) || !fileReader.finished()) {
        error = "存档长度与文件内容不一致。";
        return false;
    }
    if (checksum(payload) != storedChecksum) {
        error = "存档校验和不一致，文件可能已损坏。";
        return false;
    }
    BufferReader payloadReader(payload);
    GameState parsed;
    if (!readGameState(payloadReader, parsed, error, version == static_cast<std::uint32_t>(kSaveVersion))) return false;
    if (!payloadReader.finished()) {
        error = "存档包含未识别的尾部字段。";
        return false;
    }
    candidate = std::move(parsed);
    if (sourceVersion) *sourceVersion = version;
    error.clear();
    return true;
}

enum class LoadFileStatus { Loaded, Missing, Invalid, Unavailable };

/// 用途：读取单个文件并区分缺失、损坏与不可用。输入：路径、候选状态、错误及可选版本。输出：文件状态。
/// 状态影响：仅 Loaded 时写 candidate。失败：路径或读写错误保持候选不变；不变量：读取大小受安全上限约束。
LoadFileStatus loadFile(const std::filesystem::path& path, GameState& candidate, std::string& error,
                        std::uint32_t* sourceVersion = nullptr) {
    std::error_code code;
    const bool exists = std::filesystem::exists(path, code);
    if (code) {
        error = "无法检查文件" + path.filename().string() + "：" + code.message();
        return LoadFileStatus::Unavailable;
    }
    if (!exists) {
        error = "文件不存在：" + path.filename().string();
        return LoadFileStatus::Missing;
    }
    if (!std::filesystem::is_regular_file(path, code) || code) {
        error = "存档路径不是可读取的普通文件：" + path.filename().string();
        return LoadFileStatus::Unavailable;
    }
    const std::uintmax_t size = std::filesystem::file_size(path, code);
    if (code) {
        error = "无法读取文件大小：" + path.filename().string() + "：" + code.message();
        return LoadFileStatus::Unavailable;
    }
    if (size == 0U || size > kMaximumSaveBytes) {
        error = "存档为空或超过安全大小限制：" + path.filename().string();
        return LoadFileStatus::Invalid;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "无法打开文件：" + path.filename().string();
        return LoadFileStatus::Unavailable;
    }
    std::string data(static_cast<std::size_t>(size), '\0');
    input.read(data.data(), static_cast<std::streamsize>(data.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(data.size())) {
        error = "读取存档内容失败：" + path.filename().string();
        return LoadFileStatus::Unavailable;
    }
    return deserializeFile(data, candidate, error, sourceVersion) ? LoadFileStatus::Loaded : LoadFileStatus::Invalid;
}

/// 用途：将已验证的 .bak/.tmp 副本原子恢复为主档。输入：来源和目标路径。输出：是否成功。
/// 状态影响：成功时替换目标主档。失败：清理本次 .recover 临时文件；不变量：只接受调用方已解析的来源。
bool restoreRecoveredFile(const std::filesystem::path& source, const std::filesystem::path& destination) {
    std::filesystem::path recovery = destination;
    recovery += ".recover";
    std::error_code code;
    std::filesystem::remove(recovery, code);
    if (code) return false;
    std::filesystem::copy_file(source, recovery, std::filesystem::copy_options::none, code);
    if (code) return false;
    if (std::filesystem::exists(destination, code)) {
        if (code) return false;
        std::filesystem::remove(destination, code);
        if (code) return false;
    } else if (code) {
        return false;
    }
    std::filesystem::rename(recovery, destination, code);
    if (code) {
        std::filesystem::remove(recovery, code);
        return false;
    }
    return true;
}

/// 用途：将旧档原始字节归档为独立 .v5.bak。输入：源、目标及错误。输出：是否成功。
/// 状态影响：成功时新建历史归档。失败：不覆盖已有归档；不变量：归档文件不参与 .bak/.tmp 自动恢复。
// 步骤：确认历史归档不存在→清理专用临时文件→复制原始字节→原子改名；任一步失败均不覆盖历史档。
bool copyFileAtomically(const std::filesystem::path& source, const std::filesystem::path& destination,
                        std::string& error) {
    std::error_code code;
    if (std::filesystem::exists(destination, code)) {
        if (code) {
            error = "无法检查 v5 历史备份：" + code.message();
        } else {
            error = "v5 历史备份已存在，拒绝覆盖：" + destination.filename().string();
        }
        return false;
    }
    if (code) {
        error = "无法检查 v5 历史备份：" + code.message();
        return false;
    }
    std::filesystem::path temporary = destination;
    temporary += ".tmp";
    std::filesystem::remove(temporary, code);
    if (code) {
        error = "无法清理 v5 备份临时文件：" + code.message();
        return false;
    }
    std::filesystem::copy_file(source, temporary, std::filesystem::copy_options::none, code);
    if (code) {
        error = "无法复制 v5 原始存档：" + code.message();
        return false;
    }
    std::filesystem::rename(temporary, destination, code);
    if (code) {
        std::filesystem::remove(temporary, code);
        error = "无法提交 v5 历史备份：" + code.message();
        return false;
    }
    return true;
}

/// 用途：以验证过的 v6 临时档替换主档。输入：临时路径、目标路径和错误。输出：是否成功。
/// 状态影响：成功时提交目标主档。失败：尝试复原原主档；不变量：替换失败不得把有效主档静默丢失。
// 步骤：清理旧回滚副本→暂存旧主档→提交临时 v6→删除回滚副本；提交失败时立即把旧主档改回原名。
bool replaceMigratedPrimary(const std::filesystem::path& temporary, const std::filesystem::path& destination,
                            std::string& error) {
    std::filesystem::path previous = destination;
    previous += ".v5-migration.old";
    std::error_code code;
    std::filesystem::remove(previous, code);
    if (code) {
        error = "无法清理迁移回滚文件：" + code.message();
        return false;
    }
    const bool hadPrimary = std::filesystem::exists(destination, code);
    if (code) {
        error = "无法检查迁移前主档：" + code.message();
        return false;
    }
    if (hadPrimary) {
        std::filesystem::rename(destination, previous, code);
        if (code) {
            error = "无法暂存迁移前主档：" + code.message();
            return false;
        }
    }
    std::filesystem::rename(temporary, destination, code);
    if (code) {
        const std::string replaceError = code.message();
        if (hadPrimary) {
            std::error_code restoreCode;
            std::filesystem::rename(previous, destination, restoreCode);
            if (restoreCode) {
                error = "迁移替换失败且无法恢复原主档：" + replaceError + "；" + restoreCode.message();
                return false;
            }
        }
        error = "迁移替换主档失败：" + replaceError;
        return false;
    }
    if (hadPrimary) {
        std::filesystem::remove(previous, code);
        // 主档已成功提交时不能再把迁移报告为失败；遗留的回滚副本是可恢复的冗余文件。
    }
    return true;
}

/// 用途：将完整合法的 v5 文件安全升级为 v6。输入：来源、目标、已解析状态、错误和归档路径。输出：是否成功。
/// 状态影响：成功时替换目标并写 parsed/legacyBackup。失败：主档和 parsed 不变；不变量：历史归档保留原始字节。
// 步骤：内存序列化/复解析 v6→原子归档原始 v5→写入 v6 临时档→磁盘复解析→原子替换主档。
// 回滚点：任何替换前失败均删除本次临时档和归档；直到最后替换成功前，主档与 parsed 均保持旧值。
bool migrateV5File(const std::filesystem::path& source, const std::filesystem::path& destination, GameState& parsed,
                   std::string& error, std::filesystem::path& legacyBackup) {
    std::string v6Data;
    if (!serializeFile(parsed, v6Data, error)) return false;
    GameState memoryRoundTrip;
    if (!deserializeFile(v6Data, memoryRoundTrip, error) || memoryRoundTrip.nextItemSerial != parsed.nextItemSerial) {
        error = "v5 升级前的 v6 内存复解析失败：" + error;
        return false;
    }

    legacyBackup = destination;
    legacyBackup += ".v5.bak";
    if (!copyFileAtomically(source, legacyBackup, error)) return false;

    std::filesystem::path temporary = destination;
    temporary += ".v6-migrate.tmp";
    std::error_code code;
    std::filesystem::remove(temporary, code);
    if (code) {
        error = "无法清理 v6 升级临时文件：" + code.message();
        std::filesystem::remove(legacyBackup, code);
        return false;
    }
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "无法写入 v6 升级临时文件。";
            std::filesystem::remove(legacyBackup, code);
            return false;
        }
        output.write(v6Data.data(), static_cast<std::streamsize>(v6Data.size()));
        output.close();
        if (!output) {
            error = "关闭 v6 升级临时文件失败。";
            std::filesystem::remove(temporary, code);
            std::filesystem::remove(legacyBackup, code);
            return false;
        }
    }
    GameState diskRoundTrip;
    std::string temporaryError;
    std::uint32_t version = 0U;
    if (loadFile(temporary, diskRoundTrip, temporaryError, &version) != LoadFileStatus::Loaded ||
        version != static_cast<std::uint32_t>(kSaveVersion)) {
        std::filesystem::remove(temporary, code);
        std::filesystem::remove(legacyBackup, code);
        error = "v6 升级临时文件复解析失败：" + temporaryError;
        return false;
    }
    if (!replaceMigratedPrimary(temporary, destination, error)) {
        std::filesystem::remove(temporary, code);
        std::filesystem::remove(legacyBackup, code);
        return false;
    }
    parsed = std::move(diskRoundTrip);
    return true;
}

/// 用途：判断枚举是否为受支持的七个槽位。输入：槽位。输出：布尔值；无状态修改。
/// 失败：越界值返回 false。不变量：不会把未知枚举映射到磁盘路径。
bool validSlot(const SaveSlot slot) {
    const int value = static_cast<int>(slot);
    return value >= static_cast<int>(SaveSlot::Slot1) && value <= static_cast<int>(SaveSlot::Autosave);
}

/// 用途：生成文件最后修改时间的展示文本。输入：文件路径。输出：本地时间或“时间未知”；无状态修改。
/// 失败：读取或时区转换失败返回保守文本。不变量：不因展示失败阻断存档检查。
std::string modificationTime(const std::filesystem::path& path) {
    std::error_code code;
    const auto fileTime = std::filesystem::last_write_time(path, code);
    if (code) return "时间未知";
    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        fileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t raw = std::chrono::system_clock::to_time_t(systemTime);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &raw) != 0) return "时间未知";
#else
    if (localtime_r(&raw, &local) == nullptr) return "时间未知";
#endif
    std::ostringstream output;
    output << std::put_time(&local, "%Y-%m-%d %H:%M");
    return output.str();
}

/// 用途：把已验证状态转换为存档菜单摘要。输入：槽位、状态、来源和状态快照。输出：摘要；无状态修改。
/// 失败：无。不变量：仅复制展示字段，不触发恢复、迁移或任何文件写入。
SaveSummary summaryFor(const SaveSlot slot, const SaveStatus status, const std::filesystem::path& source,
                       const GameState& state) {
    SaveSummary summary;
    summary.slot = slot;
    summary.status = status;
    summary.modifiedAt = modificationTime(source);
    summary.mode = GameEngine::modeName(state.mode);
    summary.phase = GameEngine::phaseName(state.phase);
    summary.tribeName = state.tribeName;
    summary.leaderName = state.actingLeaderName.empty() ? state.leaderName : state.actingLeaderName;
    summary.season = state.season;
    summary.seasonLimit = state.seasonLimit;
    summary.population = state.population;
    summary.food = state.food;
    summary.wood = state.wood;
    summary.stone = state.stone;
    summary.herbs = state.herbs;
    return summary;
}

} // namespace

SaveRepository::SaveRepository(std::filesystem::path root) : root_(std::move(root)) {}

std::filesystem::path SaveRepository::pathFor(const SaveSlot slot) const {
    switch (slot) {
        case SaveSlot::Slot1:
            return root_ / "slot1.sav";
        case SaveSlot::Slot2:
            return root_ / "slot2.sav";
        case SaveSlot::Slot3:
            return root_ / "slot3.sav";
        case SaveSlot::Slot4:
            return root_ / "slot4.sav";
        case SaveSlot::Slot5:
            return root_ / "slot5.sav";
        case SaveSlot::Slot6:
            return root_ / "slot6.sav";
        case SaveSlot::Autosave:
            return root_ / "autosave.sav";
    }
    return root_ / "invalid.sav";
}

bool SaveRepository::save(const GameState& state, const SaveSlot slot, std::string& error) const {
    if (!validSlot(slot)) {
        error = "存档槽编号无效。";
        return false;
    }
    if (!GameEngine::validateState(state, error)) return false;

    std::string fileData;
    if (!serializeFile(state, fileData, error)) return false;
    GameState memoryRoundTrip;
    if (!deserializeFile(fileData, memoryRoundTrip, error)) {
        error = "存档写入前的解析校验失败：" + error;
        return false;
    }

    const std::filesystem::path path = pathFor(slot);
    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);
    if (code) {
        error = "无法创建存档目录：" + code.message();
        return false;
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::filesystem::path backup = path;
    backup += ".bak";
    std::filesystem::remove(temporary, code);
    if (code) {
        error = "无法清理旧的临时存档：" + code.message();
        return false;
    }

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "无法打开临时存档文件。";
            return false;
        }
        output.write(fileData.data(), static_cast<std::streamsize>(fileData.size()));
        output.flush();
        if (!output) {
            error = "写入临时存档失败。";
            return false;
        }
        output.close();
        if (!output) {
            error = "关闭临时存档失败。";
            return false;
        }
    }

    GameState diskRoundTrip;
    std::string temporaryError;
    if (loadFile(temporary, diskRoundTrip, temporaryError) != LoadFileStatus::Loaded) {
        std::filesystem::remove(temporary, code);
        error = "临时存档解析校验失败：" + temporaryError;
        return false;
    }

    const bool hadOriginal = std::filesystem::exists(path, code);
    if (code) {
        error = "无法检查旧存档：" + code.message();
        std::filesystem::remove(temporary, code);
        return false;
    }
    if (hadOriginal) {
        std::filesystem::remove(backup, code);
        if (code) {
            error = "无法清理旧备份：" + code.message();
            std::filesystem::remove(temporary, code);
            return false;
        }
        std::filesystem::rename(path, backup, code);
        if (code) {
            error = "无法备份旧存档：" + code.message();
            std::filesystem::remove(temporary, code);
            return false;
        }
    }

    code.clear();
    std::filesystem::rename(temporary, path, code);
    if (code) {
        const std::string renameError = code.message();
        if (hadOriginal) {
            std::error_code restoreCode;
            std::filesystem::rename(backup, path, restoreCode);
            if (restoreCode) {
                std::filesystem::remove(temporary, code);
                error = "无法替换正式存档：" + renameError + "；恢复旧存档也失败：" + restoreCode.message() +
                        "。旧数据仍保留在" + backup.filename().string() + "。";
                return false;
            }
        }
        std::filesystem::remove(temporary, code);
        error = "无法替换正式存档：" + renameError;
        return false;
    }

    // 将上一次已提交主档保留为 .bak；若新主档日后截断或损坏，它就是恢复起点。
    error.clear();
    return true;
}

bool SaveRepository::load(const SaveSlot slot, GameState& candidate, std::string& error) const {
    return load(slot, candidate, error, nullptr);
}

bool SaveRepository::load(const SaveSlot slot, GameState& candidate, std::string& error,
                          SaveLoadInfo* migrationInfo) const {
    if (migrationInfo) *migrationInfo = {};
    if (!validSlot(slot)) {
        error = "存档槽编号无效。";
        return false;
    }
    const std::filesystem::path path = pathFor(slot);
    GameState parsed;
    std::string primaryError;
    std::uint32_t primaryVersion = 0U;
    const LoadFileStatus primaryStatus = loadFile(path, parsed, primaryError, &primaryVersion);
    if (primaryStatus == LoadFileStatus::Loaded) {
        if (primaryVersion == 5U) {
            std::filesystem::path legacyBackup;
            if (!migrateV5File(path, path, parsed, error, legacyBackup)) return false;
            if (migrationInfo) *migrationInfo = {true, std::move(legacyBackup)};
        }
        candidate = std::move(parsed);
        error.clear();
        return true;
    }
    if (primaryStatus == LoadFileStatus::Unavailable) {
        error = "读取" + slotName(slot) + "失败。主文件暂时不可用：" + primaryError +
                "。为避免误读旧备份，本次没有自动回退。";
        return false;
    }

    std::filesystem::path backup = path;
    backup += ".bak";
    std::filesystem::path temporary = path;
    temporary += ".tmp";

    std::string backupError;
    std::uint32_t backupVersion = 0U;
    if (loadFile(backup, parsed, backupError, &backupVersion) == LoadFileStatus::Loaded) {
        if (backupVersion == 5U) {
            std::filesystem::path legacyBackup;
            if (!migrateV5File(backup, path, parsed, error, legacyBackup)) return false;
            if (migrationInfo) *migrationInfo = {true, std::move(legacyBackup)};
        } else if (!restoreRecoveredFile(backup, path)) {
            error = "已验证" + backup.filename().string() + "，但恢复主档失败；当前游戏状态未改变。";
            return false;
        }
        candidate = std::move(parsed);
        error.clear();
        return true;
    }

    std::string temporaryError;
    std::uint32_t temporaryVersion = 0U;
    if (loadFile(temporary, parsed, temporaryError, &temporaryVersion) == LoadFileStatus::Loaded) {
        if (temporaryVersion == 5U) {
            std::filesystem::path legacyBackup;
            if (!migrateV5File(temporary, path, parsed, error, legacyBackup)) return false;
            if (migrationInfo) *migrationInfo = {true, std::move(legacyBackup)};
        } else if (!restoreRecoveredFile(temporary, path)) {
            error = "已验证" + temporary.filename().string() + "，但恢复主档失败；当前游戏状态未改变。";
            return false;
        }
        candidate = std::move(parsed);
        error.clear();
        return true;
    }

    error = "读取" + slotName(slot) + "失败。主文件：" + primaryError + "；备份文件：" + backupError + "；临时文件：" +
            temporaryError;
    return false;
}

std::vector<SaveSummary> SaveRepository::inspect() const {
    // 列表只解析文件，不调用可能修复主档的 load；候选顺序与实际读取一致。
    constexpr std::array<SaveSlot, 7> slots{{SaveSlot::Autosave, SaveSlot::Slot1, SaveSlot::Slot2, SaveSlot::Slot3,
                                             SaveSlot::Slot4, SaveSlot::Slot5, SaveSlot::Slot6}};
    std::vector<SaveSummary> summaries;
    summaries.reserve(slots.size());
    for (const SaveSlot slot : slots) {
        const std::filesystem::path primary = pathFor(slot);
        std::filesystem::path backup = primary;
        backup += ".bak";
        std::filesystem::path temporary = primary;
        temporary += ".tmp";

        GameState state;
        std::string primaryError;
        const LoadFileStatus primaryStatus = loadFile(primary, state, primaryError);
        if (primaryStatus == LoadFileStatus::Loaded) {
            summaries.push_back(summaryFor(slot, SaveStatus::Ready, primary, state));
            continue;
        }
        if (primaryStatus == LoadFileStatus::Unavailable) {
            SaveSummary summary;
            summary.slot = slot;
            summary.status = SaveStatus::Corrupt;
            summaries.push_back(std::move(summary));
            continue;
        }

        std::string backupError;
        const LoadFileStatus backupStatus = loadFile(backup, state, backupError);
        if (backupStatus == LoadFileStatus::Loaded) {
            summaries.push_back(summaryFor(slot, SaveStatus::Recoverable, backup, state));
            continue;
        }
        std::string temporaryError;
        const LoadFileStatus temporaryStatus = loadFile(temporary, state, temporaryError);
        if (temporaryStatus == LoadFileStatus::Loaded) {
            summaries.push_back(summaryFor(slot, SaveStatus::Recoverable, temporary, state));
            continue;
        }

        SaveSummary summary;
        summary.slot = slot;
        const bool allMissing = primaryStatus == LoadFileStatus::Missing && backupStatus == LoadFileStatus::Missing &&
                                temporaryStatus == LoadFileStatus::Missing;
        summary.status = allMissing ? SaveStatus::Empty : SaveStatus::Corrupt;
        summaries.push_back(std::move(summary));
    }
    return summaries;
}

std::optional<SaveSlot> SaveRepository::parseSlot(const std::string_view text) {
    if (text == "1" || text == "slot1" || text == "存档1") return SaveSlot::Slot1;
    if (text == "2" || text == "slot2" || text == "存档2") return SaveSlot::Slot2;
    if (text == "3" || text == "slot3" || text == "存档3") return SaveSlot::Slot3;
    if (text == "4" || text == "slot4" || text == "存档4") return SaveSlot::Slot4;
    if (text == "5" || text == "slot5" || text == "存档5") return SaveSlot::Slot5;
    if (text == "6" || text == "slot6" || text == "存档6") return SaveSlot::Slot6;
    if (text == "auto" || text == "autosave" || text == "自动" || text == "自动档") {
        return SaveSlot::Autosave;
    }
    return std::nullopt;
}

std::string SaveRepository::slotName(const SaveSlot slot) {
    switch (slot) {
        case SaveSlot::Slot1:
            return "手动存档1";
        case SaveSlot::Slot2:
            return "手动存档2";
        case SaveSlot::Slot3:
            return "手动存档3";
        case SaveSlot::Slot4:
            return "手动存档4";
        case SaveSlot::Slot5:
            return "手动存档5";
        case SaveSlot::Slot6:
            return "手动存档6";
        case SaveSlot::Autosave:
            return "自动存档";
    }
    return "未知存档";
}

} // namespace tribe
