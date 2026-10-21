# src/core/git/

## 职责

`GitService` 是 NexusAI 唯一直接调用 git 的同步服务：仓库探测、历史/差异/署名/图谱/分支/标签只读查询，以及少量"用户明确触发"的写操作（暂存、提交、丢弃、amend、undo、stash、按 hunk 暂存）。

- 无会话状态：每次调用新建轻量实例，用完即弃。
- `projectRoot` 为空时退回 `Sandbox::currentProjectDir()`，再退回进程工作目录（`GitService.cpp:135`、`baseDir()`）。
- 返回普通值类型 + `QJsonObject toJson()`，由 viewmodel 转 `QVariantMap` 给 QML。
- core 层不反向依赖 viewmodels；唯一外部依赖是 `core/tools/Sandbox.h`。
- 职责边界：不 fetch、不 push、不 checkout、不建/删分支、不改 remote。

## 设计（模式/抽象/关键类型）

- **值对象**（`GitService.h`）：`GitCommitInfo`、`GitFileStat`、`GitStatusEntry`、`GitBlameLine`、`GitLogQuery`、`GitStashEntry`、`GitHunk`/`GitHunkRef`、`GitTagInfo`、`GitGraphLine`、`GitBranchInfo`、`GitRemoteInfo`。
- 每个值对象都有 `isValid()` 与 `toJson()`；`GitBlameLine::uncommitted()` 以全零 SHA 识别"仅存在于工作区"的行。
- `GitHunkRef{index, header}` 同时用位置与旧头部寻址，供 `applyHunks` 做"列表已过期"检测。
- **构造参数即策略**：`(projectRoot, outputLimitBytes=2 MiB, timeoutMs=8000)`，内部 `qMax` 夹到 1024 / 100。
- **进程封装**：私有 `CommandResult{ok,exitCode,output,error,truncated,timedOut}`。
- 两个执行入口：`run(args)` 与 `runWithInput(args,input)`，后者供 `git apply` 从 stdin 读补丁。
- **解析集中化**：静态 `parseLog`、`parseNameStatus`+`applyNumStat`、`parseHunks`、`parseBlame`、`parseTrack`；读命令只拼参数。
- **命令即 argv**：`QProcess::setProgram("git") + setArguments(QStringList)`，全程无 shell。
- 因此路径不靠手工引号，而靠"参数列表 + 路径校验"保证安全，空格/引号由 QProcess 的 argv 传递语义处理。
- **读写边界**：读操作用 `GIT_OPTIONAL_LOCKS=0` 不刷新 index；写操作同环境但会取 index 锁。

## 数据与控制流

1. `run()`：工作目录 = `baseDir()`，`SeparateChannels`，注入 `GIT_OPTIONAL_LOCKS=0`、`GIT_TERMINAL_PROMPT=0`、`GIT_PAGER=cat`、`LC_ALL=C`（`GitService.cpp:184-191`）。
2. `waitForStarted(3000)` 失败即返回 `"Failed to start git: …"`。
3. 有 stdin 时 `write` + `waitForBytesWritten(3000)`，随后 `closeWriteChannel()`，git 不会等终端。
4. 轮询：`waitForReadyRead(50)` → drain 双通道。
5. stdout 超 `m_outputLimitBytes`：立即 `kill()` 并置 `truncated`。
6. 累计超 `m_timeoutMs`：置 `timedOut`，`kill()` + `waitForFinished(1000)`。
7. `ok = exitCode==0 || truncated`；超时强制 false。
8. `error` 取 stderr（空则 `"git exited with code N"`），stderr 上限 64 KiB。
9. 调用方统一用 `QString* error` 出参，空串即成功；错误文本直接来自 git，便于面板原样展示。
10. 每次高层查询几乎都先跑 `repositoryRoot()`（`rev-parse --show-toplevel`），所以常见 2~3 个进程；`repoInfo()` 固定 4 个。
11. 解析全部基于固定分隔符（`%x00`、`\x1f`、`-z`），不依赖人读排版。
12. `graph` 用 git 自己输出的 NUL 标记图形前缀终点，不需要猜 `|`/`\` 的归属。

**操作清单**

- 仓库：`isGitAvailable()`（`git --version`，3 s 启动 / 5 s 完成）、`isRepository`、`repositoryRoot`（canonical 化）、`repoInfo()` → `{isRepo,root,branch,detached,head,dirty,error}`。
- 历史：`log()` 两个重载；limit 夹 1..500，`--skip`、`--date=unix`。
- 历史过滤：`grep`/`author` 用 `-F -i` 固定串，二者需同时命中（对应 git 默认的跨类限制）。
- `fileHistory()`：`--follow` 跟随重命名，上限同样 1..500。
- `commit(sha)`：`show -s`，6 个固定 NUL 字段 + subject + 多行 body。
- 差异：`commitFiles` = `show --name-status -z --find-renames -m --first-parent`，第二遍 `--numstat` 补行数。
- `compareFiles`：`diff --name-status/--numstat -z`，用于两提交对比。
- `commitDiff`/`compareDiff`：`--no-color --no-ext-diff --no-textconv --unified=3`，用 `truncated` 出参区分被截断。
- 工作区：`status()`（`--porcelain=v1 -z --untracked-files=all --renames`；`R/C` 后一个 token 是 oldPath）。
- `statusText()`：`-b` 版 porcelain，**失败返回 null QString**，空串才是"干净"。
- `workingDiff(path,staged)`：对未跟踪文件自行合成 `--- /dev/null` 的全新增 diff；出现 NUL 即判二进制并报错。
- 写：`stage`（`add --`）、`unstage`（`reset -q HEAD --`，兼容 unborn 分支）。
- `commitStaged`：先 `diff --cached --name-only` 防空提交，再 `commit -m`，不加 `--no-verify`（hook 必须跑）。
- `discardPath`：已跟踪 → `restore --`（保留 index 内容）；未跟踪 → 复制到 `backupRoot/<epoch>/<rel>`，校验大小后再删。
- `discardPath` 的 `backupRoot` 为空时直接拒绝，避免"点了就没了"。
- 改写：`amendLast`、`undoLastCommit`；都要求 HEAD 存在且 `headOnRemote()` 为假。
- `headOnRemote()`：`branch -r --contains HEAD`，只看本地已有的 remote-tracking ref，不 fetch。
- `undoLastCommit`：先 `rev-list --parents -n 1`，拒绝根提交与 merge，再 `reset --soft HEAD^`。
- stash：`stashPush` 先两次 `diff --quiet` 判断 tracked 变更，避免 git 在无变更时静默成功。
- `stashList`：`--format=%gd\x1f%s\x1f%ct`；`stashPop` 先校验索引再 `stash pop stash@{n}`。
- 局部暂存：`diffHunks` = `workingDiff` + `parseHunks`；新增/删除/重命名/二进制整文件拒绝。
- `applyHunks` **重读当前 diff**，同时校验 index 与旧 header，再拼补丁。
- 应用方式：`git apply --cached [--reverse] --whitespace=nowarn`，不匹配整体失败，不半应用。
- 引用视图：`branches()` 用 `for-each-ref refs/heads`，`upstream:track` 解析 ahead/behind/gone/diverged/synced。
- `remotes()`：`remote` + `config --get remote.N.url` + `for-each-ref refs/remotes/N`；每 remote ≤200 分支。
- `remotes()` 的 URL 经 `maskedUrl()` 去掉 user:password，覆盖 `https://`、`ssh://` 与 scp 形式。
- `tags()`：annotated/lightweight 一趟 `for-each-ref refs/tags`（`objecttype`/`*objectname`/`creatordate`）。
- `blame()`：`--line-porcelain`，40 位 SHA 头正则；截断或未跟踪文件直接报错而非半份归属。
- `graph()`：`log --graph --all -n`，上限 300，纯图形行保留以保证画面连贯。

## 集成点

- `src/viewmodels/GitViewModel.*`：QML 单例 `GitVM`。
- 每个调用经 `QtConcurrent::run` 进线程池（`GitViewModel.cpp:58-68`），结果用带 `requestId` 的信号返回。
- `startWriteOperation()` 提供独占写锁（`m_operationBusy`）、60 s 超时、成功后自动 `refreshStatus/refreshRepoInfo`。
- `rootGeneration`/`writeGeneration` 在根目录切换或新写操作时丢弃过期回调。
- `src/core/tools/ToolRegistry.cpp`：`git_status`/`git_diff`（Safe）直接 `GitService(Sandbox::currentProjectDir())`。
- `git_commit`（RequiresConfirm）走的是 ToolRegistry 自己的 `runGit()`，不经过本服务。
- `src/core/tools/Sandbox.h`：默认项目根来源，工具层先 `Sandbox::resolvePath()` 再交给本服务。
- `CMakeLists.txt:53-54`：编入 `nexus_core`。
- 测试：`tests/test_git_service.cpp`（16 例）、`test_git_viewmodel.cpp`、`test_git_panel_qml.cpp`。

## 关键文件

- `GitService.h`（339 行）：类型契约、公开 API、私有解析/校验声明。
- `GitService.cpp`（1841 行）：进程循环 171-251。
- 路径校验 329-386；SHA 校验 320-324；URL 脱敏 1694-1714。
- 解析器：388-405（log）、561-614（name-status/numstat）、697-733（blame）、1491-1593（hunks）。
- 写操作：1120-1489（stage/commit/amend/undo/stash/discard）、1625-1692（applyHunks）。
- 引用视图：1749-1839（branches/remotes），840-884（tags），785-838（graph）。
- `src/viewmodels/GitViewModel.h:16-52`（线程/generation 语义）、`GitViewModel.cpp:382-438`（写操作）。
- `tests/test_git_service.cpp`、`tests/test_git_viewmodel.cpp`、`tests/test_git_panel_qml.cpp`。

## 约束与风险

- **取消只有超时**：没有 `cancel()` API。
- 8 s 读超时会 kill git；写路径由 ViewModel 放宽到 60 s。
- `rootGeneration`/`writeGeneration` 只丢弃过期结果，并不中止已在跑的进程。
- **路径安全**：`normalizePath` 拒绝首字符 `-`/`:`（防选项与 pathspec magic）、拒绝 `..`。
- 绝对路径转仓库相对路径并做 `isWithin` 前缀检查（Windows 大小写不敏感）。
- `normalizePaths` 去重且要求至少一个有效项，否则报 `"A repository path is required"`。
- **SHA 安全**：`validSha` = `^[0-9a-fA-F]{4,64}$`，短 SHA 合法。
- **无注入面**：参数均以 argv 传递，`stash@{n}` 也是单参数，不经 shell。
- **编码假设**：输出统一 `QString::fromUtf8` 且 `LC_ALL=C`。
- 非 `-z` 输出（`statusText`、`graph`、blame 文本）在非 UTF-8 文件名/内容下可能乱码或被 `core.quotepath` 转义。
- `-z` 路径同样按 UTF-8 解码，Windows GBK 路径存在风险。
- **截断仍算成功**：`ok = exitCode==0 || truncated`。
- `parseLog` 跳过字段不足 7 的尾行，大仓库可能静默少几条；blame/graph 则显式报错。
- **`git_commit` 绕过本服务**：`ToolRegistry.cpp:925` 的 `runGit()` 自起 `QProcess`（MergedChannels、10 s、无路径校验、无 stdout 上限）。
- **引用边界**：不 fetch，`headOnRemote` 只看本地已抓取的引用；久未 fetch 时已发布提交可能被误判为可改写。
- **性能**：无缓存，每次读都重新 `rev-parse`；写操作只在 ViewModel 层串行，`GitService` 本身可被并发调用。
- **备份可靠性**：`discardPath` 仅按文件大小校验备份副本，随后直接删除源文件。
