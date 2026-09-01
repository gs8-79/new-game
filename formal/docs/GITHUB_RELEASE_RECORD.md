# GitHub 版本发布记录

仓库：<https://github.com/gs8-79/new-game.git>

发布日期：2026-09-01（北京时间）

## 发布顺序与远端结果

| 顺序 | 版本 | 提交 | 远端验证 |
|---:|---|---|---|
| 1 | `v1.0.0-expansion-preview` | `eb0c19bc9bfd1ea641f05abe0cadf2168d36d07b` | 标签解引用到该提交；发布V1时`main`也指向该提交 |
| 2 | `v2.0.0-expansion` | `0efc99d510d1b6a8af07376d3ae32350c1d31f33` | 标签解引用到该提交；发布V2时`main`也指向该提交 |

V1先发布并核对，随后才发布V2。因此两个标签分别保留了可独立检出的课程迭代节点，而不是只在最终状态补两个无法对应过程的名称。

## 标签对象

两个版本均使用带注释标签。远端查询结果为：

```text
45d8eae135a92cc26b88f3099309b03f773dd21f refs/tags/v1.0.0-expansion-preview
eb0c19bc9bfd1ea641f05abe0cadf2168d36d07b refs/tags/v1.0.0-expansion-preview^{}
9a07bbf88d4f6aacc52d6044b1aa9be2aea2d2fa refs/tags/v2.0.0-expansion
0efc99d510d1b6a8af07376d3ae32350c1d31f33 refs/tags/v2.0.0-expansion^{}
```

其中不带`^{}`的是注释标签对象，带`^{}`的是标签实际指向的提交。

## 证据边界

本记录证明代码和标签已经到达上述GitHub仓库，并且远端哈希与本地版本节点一致。它不等于GitHub Release附件、Mac实机构建、交换小组试玩、课堂验收或生产发布；这些项目仍按各自材料单独验证。
