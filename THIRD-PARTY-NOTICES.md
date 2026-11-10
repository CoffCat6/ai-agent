# 第三方组件声明（THIRD-PARTY NOTICES）

NexusAI 以 MIT 许可证发布（见 LICENSE）。发布物（安装包 / zip / AppImage）
捆绑了以下第三方运行时组件，其许可证各自适用：

## Qt 6.9.0

- © The Qt Company Ltd. 及 Qt 贡献者
- 许可证：**LGPL-3.0**（开源版；Qt 亦可按 GPL-3.0 / 商业许可使用，本产品
  以动态链接方式使用 Qt，遵循 LGPL 条款）
- 完整许可证文本：<https://www.gnu.org/licenses/lgpl-3.0.html>
- 对应源代码（Qt 6.9.0，用于 LGPL 第 4 条之来源提供）：
  <https://download.qt.io/archive/qt/6.9/6.9.0/>
- LGPL 合规说明：Qt 以独立共享库形式动态链接并随包原样分发，未做静态
  链接或修改；替换 Qt 库即可实现 LGPL 要求的用户再链接能力。

## qtkeychain

- © 2011-2015 Frank Osterfeld <frank.osterfeld@gmail.com>
- 许可证：**BSD 3-Clause**

  > Redistribution and use in source and binary forms, with or without
  > modification, are permitted provided that the following conditions are
  > met:
  >
  > 1. Redistributions of source code must retain the above copyright
  >    notice, this list of conditions and the following disclaimer.
  > 2. Redistributions in binary form must reproduce the above copyright
  >    notice, this list of conditions and the following disclaimer in the
  >    documentation and/or other materials provided with the distribution.
  > 3. Neither the name of the copyright holder nor the names of its
  >    contributors may be used to endorse or promote products derived from
  >    this software without specific prior written permission.
  >
  > THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
  > IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
  > TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
  > PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  > HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  > SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  > LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  > DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  > THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  > (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  > OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

- 上游：<https://github.com/frankosterfeld/qtkeychain>

## 构建工具（不随发布物分发）

以下工具仅用于 CI/本地构建，不进入任何发布物，其许可证不影响本产品的
分发：Inno Setup（安装包编译器）、appimagetool（AppImage 封装）、
CMake/Ninja/GCC/MinGW。
