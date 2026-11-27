---
name: hermit-build-test
description: Hermit リポジトリで Visual Studio の Hermit/test プロジェクトをビルドし、test.exe で Google Test の単体テストを実行する。
metadata:
  short-description: Hermit のビルドと単体テスト
---

# Hermit のビルドと単体テスト

Hermit リポジトリで、ユーザーがアプリケーションまたはテストのビルド、あるいは単体テスト実行を依頼したときに使う。

## ビルド

- コマンド実行前にリポジトリの `AGENTS.md` と最新の `.vcxproj` を確認し、作業対象の制約に従う。
- 通常のビルド・テスト検証は差分ビルドを優先し、`/t:Build`（またはターゲット省略）を使用する。`/t:Rebuild` は、ツールチェーン変更、生成先変更、依存関係破損、クリーン状態の再現が必要な場合だけ実行し、理由を報告する。
- チャットに接続された PTY で、インストール済み Visual Studio 18 の `VsDevCmd.bat -arch=x64 -host_arch=x64` を呼び出して環境を初期化し、同じセッションの PATH 上の `msbuild` を実行する。Visual Studio 2019/2022 の開発者環境や通常の PowerShell の環境を混在させず、`MSPDB140.DLL` の異なるバージョンを PATH に混在させない。
- `VsDevCmd.bat` の場所はインストール済み Visual Studio 18 の情報から解決し、固定パスを前提にしない。初期化後、同じ PTY で `where msbuild` により Visual Studio 18 の MSBuild を確認してから、`/p:VisualStudioVersion=18.0`、`/p:Configuration=Release`、`/p:Platform=x64` を明示して実行する。
- Release x64 の通常ビルドは `/m:1` とする。テストプロジェクトで共有 `test.pdb` への並列コンパイル書き込みが C1041 を起こした履歴がある。並列性能の計測が目的の場合だけ別途 `/m` を使い、PDB/中間出力の競合がないことを先に確認する。
- C1041 が出た場合、test.pdb が他プロセスに保持されていないか確認する。`/m:1` だけでは解消しない場合は `/p:MultiProcessorCompilation=false` で Release x64 solution Build を再試行する。この設定で2026-11-27に PDB/LTCG を保った Build と全テストが成功した。`CL_MPCount=1` 単独では今回効果がなかった。再発時は保持プロセスのPIDを記録し、終了を確認する。
- Hermit アプリの Release x64 リンクでは `.vcxproj` の `LinkTimeCodeGeneration=UseLinkTimeCodeGeneration` により `/LTCG`（非 incremental LTCG）を使用する。`/LTCG:incremental` では更新した HermitLib オブジェクトに対する差分リンクで LNK1103 が発生したため、PDB と LTCG を維持したまま full LTCG に統一した。この設定を変更する場合は、ソース1翻訳単位の差分再コンパイルとリンクを検証する。
- 検証構成は Release x64 に固定する。ソリューション内の両プロジェクトをビルドする:

  ```powershell
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  $install = & $vswhere -products * -property installationPath | Where-Object { $_ -match '\\18\\' } | Select-Object -First 1
  $devcmd = Join-Path $install 'Common7\Tools\VsDevCmd.bat'
  $buildCommand = 'call "{0}" -arch=x64 -host_arch=x64 && where msbuild && msbuild .\Hermit.sln /m:1 /t:Build /p:VisualStudioVersion=18.0 /p:Configuration=Release /p:Platform=x64 /p:MultiProcessorCompilation=false /v:normal && echo BUILD_SUCCEEDED' -f $devcmd
  cmd.exe /d /c $buildCommand
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  ```

- ビルドはチャットに紐づく PTY/コンソールセッションで開始する。独立した非表示バックグラウンドプロセスや切断される `Start-Process` で起動しない。セッション ID を保持し、開始直後と 10 秒以内ごとのポーリングでコンソール出力・実行状態・終了コードを確認する。
- `/v:normal` で出力し、開始・各プロジェクト・コンパイル/リンク・最終要約を確認する。終了コード `0` と `BUILD_SUCCEEDED` または MSBuild の成功要約の両方を確認して完了とする。
- 2026-11-26 の `/m`・`/v:minimal` 試行はMSBuildエラー診断を残さず、進捗がチャットへ届かないまま中断され、終了コード `-1073741510`（Ctrl+C）になった。これはソースのビルド失敗の証拠ではない。同日に `/m:1`・`/v:normal` の PTY 実行は Release x64 solution で成功した。出力が一時的に途切れても失敗と推定せず、セッションをポーリングし、必要なら直列・詳細出力のコマンドで再実行する。
- セッションが切断された場合は、プロセスの完了を推測せず、再接続または再実行が必要な状態として報告する。

- アプリだけを Release x64 でビルドする場合:

  ```powershell
  $buildCommand = 'call "{0}" -arch=x64 -host_arch=x64 && where msbuild && msbuild .\Hermit\Hermit.vcxproj /m:1 /t:Build /p:VisualStudioVersion=18.0 /p:Configuration=Release /p:Platform=x64 /p:MultiProcessorCompilation=false /v:normal && echo BUILD_SUCCEEDED' -f $devcmd
  cmd.exe /d /c $buildCommand
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  ```

- テストプロジェクトだけを Release x64 でビルドする場合:

  ```powershell
  $buildCommand = 'call "{0}" -arch=x64 -host_arch=x64 && where msbuild && msbuild .\test\test.vcxproj /m:1 /t:Build /p:VisualStudioVersion=18.0 /p:Configuration=Release /p:Platform=x64 /p:MultiProcessorCompilation=false /v:normal && echo BUILD_SUCCEEDED' -f $devcmd
  cmd.exe /d /c $buildCommand
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  ```

  `test.vcxproj` は Hermit プロジェクトを参照しているため、必要に応じて Hermit もビルドされる。
- 両プロジェクトはルートの `vcpkg.json` を使う。vcpkg の MSBuild 連携が有効なら、ビルド時に Boost と Google Test が自動復元される。`vcpkg integrate install` はユーザー環境に対する一度だけの設定であり、プロジェクトのビルドイベントには追加しない。
- コンパイル時に Boost / Google Test のヘッダーが見つからない場合は、まず各 `.vcxproj` の `VcpkgRoot`、`vcpkg.props`（`Microsoft.Cpp.props` 後）、`vcpkg.targets`（`Microsoft.Cpp.targets` 後）の条件付きインポートを確認する。復元先の `vcpkg_installed/` は手動で閲覧・編集・削除しない。復元は vcpkg に任せる。
- 中間生成物の共有警告が出た場合は、両プロジェクトに設定された `$(SolutionDir)$(Platform)\$(Configuration)\obj\$(MSBuildProjectName)\` の `IntDir` と、Release 構成のプロジェクト別 PDB パスを確認する。

## 単体テスト

- テスト実行はユーザーから実行または検証を依頼された場合に限る。
- 必ず Release x64 のビルド成功後に、ソリューションルートから次を実行する:

  ```powershell
  $test = Start-Process -FilePath '.\test\x64\Release\test.exe' -NoNewWindow -PassThru
  if (-not $test.WaitForExit(300000)) {
      & taskkill.exe /PID $test.Id /T /F
      if (-not $test.WaitForExit(10000)) { throw "test.exe PID $($test.Id) did not exit after taskkill." }
      throw "test.exe exceeded 5-minute limit (PID $($test.Id)); process tree was terminated."
  }
  $test.Refresh()
  if ($test.ExitCode -ne 0) { throw "test.exe failed with exit code $($test.ExitCode)." }
  ```

- loopback runtime tests を含む全テストは sandbox 内で起動せず、sandbox 外のチャット接続 PTY で実行する。sandbox 内の loopback 失敗がテストプロセスを残し、同じ `test.exe` の再リンクを LNK1104 にした実績がある。
- テストプロセスには5分の上限を設定し、上限を超えた場合は `taskkill /PID <pid> /T /F` で子プロセスを含めて終了し、PID が消えたことを確認する。PowerShell では `Start-Process -NoNewWindow -PassThru` で PTY に接続して起動し、`WaitForExit(300000)` の結果を確認する。時間切れのプロセスを残したまま作業を終えない。
- Google Test のテスト別出力とプロセス終了コードを確認する。終了コード 0 は成功。失敗時は失敗したテスト名と関連する診断を報告する。
- テストプロセスも実行セッションを保持し、完了までポーリングする。終了コードを取得できない場合は結果未確定として報告する。
- runtime の既存 TCP overload を検証するテストは `127.0.0.1` の一時 loopback mock server のみを使う。listener が loopback と一時ポートのみを使い、外部接続を行わないことを確認する。sandbox 外でも失敗する場合は、server と socket の診断を記録し、成功と推測しない。
- `test.exe` への LNK1104 が発生したら、最初に実行中の test プロセスと親コマンドを特定する。Restart Manager または同等のハンドル照会を使い、当該 workspace の古いテストプロセスと確認できたものだけを終了する。ACL 変更や別名成果物への恒久切替で原因を隠さず、既定名への再リンクで解消を確認する。
- 実行ファイルが上記の既定位置にない場合は、プロジェクトの `OutDir` 設定で場所を確認する。`vcpkg_installed/` は検索しない。

## 結果の報告

Release x64 でビルドしたプロジェクト、依存復元の有無、テストを実行した場合は結果の概要と終了コードを伝える。Release x64 以外の構成での検証結果は、このスキルの検証結果として扱わない。失敗時は原因となる診断を簡潔に添える。
