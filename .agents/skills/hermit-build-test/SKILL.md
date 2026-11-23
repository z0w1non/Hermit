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
- 検証構成は Release x64 に固定する。ソリューション内の両プロジェクトをビルドする:

  ```powershell
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  $install = & $vswhere -products * -property installationPath | Where-Object { $_ -match '\\18\\' } | Select-Object -First 1
  $devcmd = Join-Path $install 'Common7\Tools\VsDevCmd.bat'
  cmd.exe /d /c "call \"$devcmd\" -arch=x64 -host_arch=x64 && where msbuild && msbuild .\Hermit.sln /m /t:Build /p:VisualStudioVersion=18.0 /p:Configuration=Release /p:Platform=x64 /v:minimal"
  ```

- 並列ビルド（`/m`）は、チャットに紐づく PTY/コンソールセッションへ接続した状態で開始する。独立した非表示バックグラウンドプロセスや切断される `Start-Process` で起動しない。セッション ID を保持し、開始直後と 10 秒以内ごとのポーリングでコンソール出力・実行状態・終了コードを確認する。
- ビルドコマンドは進捗が確認できる最小出力（`/v:minimal` など）で実行し、プロジェクト名、コンパイル対象、コード生成、リンク結果がコンソールに表示されるようにする。
- セッションが切断された場合は、プロセスの完了を推測せず、再接続または再実行が必要な状態として報告する。

- アプリだけを Release x64 でビルドする場合:

  ```powershell
  cmd.exe /d /c "call \"$devcmd\" -arch=x64 -host_arch=x64 && where msbuild && msbuild .\Hermit\Hermit.vcxproj /t:Build /p:VisualStudioVersion=18.0 /p:Configuration=Release /p:Platform=x64 /v:minimal"
  ```

- テストプロジェクトだけを Release x64 でビルドする場合:

  ```powershell
  cmd.exe /d /c "call \"$devcmd\" -arch=x64 -host_arch=x64 && where msbuild && msbuild .\test\test.vcxproj /t:Build /p:VisualStudioVersion=18.0 /p:Configuration=Release /p:Platform=x64 /v:minimal"
  ```

  `test.vcxproj` は Hermit プロジェクトを参照しているため、必要に応じて Hermit もビルドされる。
- 両プロジェクトはルートの `vcpkg.json` を使う。vcpkg の MSBuild 連携が有効なら、ビルド時に Boost と Google Test が自動復元される。`vcpkg integrate install` はユーザー環境に対する一度だけの設定であり、プロジェクトのビルドイベントには追加しない。
- コンパイル時に Boost / Google Test のヘッダーが見つからない場合は、まず各 `.vcxproj` の `VcpkgRoot`、`vcpkg.props`（`Microsoft.Cpp.props` 後）、`vcpkg.targets`（`Microsoft.Cpp.targets` 後）の条件付きインポートを確認する。復元先の `vcpkg_installed/` は手動で閲覧・編集・削除しない。復元は vcpkg に任せる。
- 中間生成物の共有警告が出た場合は、両プロジェクトに設定された `$(SolutionDir)$(Platform)\$(Configuration)\obj\$(MSBuildProjectName)\` の `IntDir` と、Release 構成のプロジェクト別 PDB パスを確認する。

## 単体テスト

- テスト実行はユーザーから実行または検証を依頼された場合に限る。
- 必ず Release x64 のビルド成功後に、ソリューションルートから次を実行する:

  ```powershell
  & .\test\x64\Release\test.exe
  ```

- Google Test のテスト別出力とプロセス終了コードを確認する。終了コード 0 は成功。失敗時は失敗したテスト名と関連する診断を報告する。
- テストプロセスも実行セッションを保持し、完了までポーリングする。終了コードを取得できない場合は結果未確定として報告する。
- 実行ファイルが上記の既定位置にない場合は、プロジェクトの `OutDir` 設定で場所を確認する。`vcpkg_installed/` は検索しない。

## 結果の報告

Release x64 でビルドしたプロジェクト、依存復元の有無、テストを実行した場合は結果の概要と終了コードを伝える。Release x64 以外の構成での検証結果は、このスキルの検証結果として扱わない。失敗時は原因となる診断を簡潔に添える。
