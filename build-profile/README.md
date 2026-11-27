# Release x64 リビルド計測（2026-11-23）

Visual Studio 2026 18.12.1、MSVC 14.52.37025、MSBuild 18.12.3 を使用した。`VsDevCmd.bat -arch=x64 -host_arch=x64` で初期化した同一 PTY から、次の条件で計測した。

- 構成: `Release|x64`
- ターゲット: `Rebuild`
- 並列度: `/m:1`（翻訳単位の時間を分離するため）
- コンパイラ計測: `_CL_=/Bt+`
- 結果: 成功、警告 0、エラー 0、経過時間 202.44 秒

## 集計

| 区分 | 時間 | 全体比 |
|---|---:|---:|
| `ClCompile` | 157.92 秒 | 78.0% |
| `Link` | 41.41 秒 | 20.5% |
| その他 | 3.11 秒 | 1.5% |

`/Bt+` でファイルに帰属できたコンパイラ時間は 151.41 秒で、残りはコンパイラ起動・入出力などのタスク内オーバーヘッドである。上位 3 翻訳単位が帰属可能時間の 38.5% を占める。

## ボトルネック

1. `Hermit_parser.cpp`（28.84 秒）
   - ほぼ全てがフロントエンド（28.63 秒）。コード生成は 0.22 秒にすぎない。
   - 642 行の本体に加え、Boost.Spirit.Qi 文法と大量の visitor/template を持つ 800 行超の `Hermit_parser_grammar_impl.hpp` を直接取り込む。テンプレート解析・実体化が主因と判断できる。
   - 最優先候補は、文法の明示的実体化を `.cpp` に閉じ込め、公開ヘッダーから Spirit の重い定義を隠すこと。次点は parser 実装を責務単位に分割すること。

2. `test/test.cpp`（17.19 秒）
   - フロントエンド 13.30 秒、バックエンド 3.89 秒で、他の翻訳単位と異なりコード生成も重い。
   - 774 行・全テストを単一翻訳単位に集約し、Google Test/Mock、Boost.Program_options、Hermit の多数の公開ヘッダー、`Hermit_lru_impl.hpp` を一度に読み込む。
   - テストを機能別 `.cpp` に分割すると、変更時の差分ビルドを小さくできる。クリーンビルドは並列化で短縮できるが、総 CPU 時間自体は必ずしも減らない。

3. `Hermit_builtin.cpp`（12.20 秒）
   - ほぼ全てがフロントエンド（12.16 秒）。Boost.Asio/Process/DateTime、parser、LLM、config など異なる責務の重い依存を同時に含む。
   - 組み込み関数群を、プロセス・環境変数・日時・入出力などの責務別実装へ分割すると、直接依存と差分コンパイル範囲を狭められる。

4. リンク（合計 41.41 秒）
   - Release の `/GL` + `/LTCG` により、Hermit の初回リンクで 53,529 関数が全再コンパイルされた。アプリとテストのプロジェクト所要時間はそれぞれ 24.57 秒、40.48 秒である。
   - `vcpkg` の全静的ライブラリをワイルドカードでリンク候補に含めた状態で LTCG を実施している。機能・性能要件を保ったまま、実際に必要なライブラリだけを列挙できるかを調査する価値がある。

## 推奨順序

1. parser のテンプレート境界を整理し、明示的実体化または非テンプレート境界で Spirit を隠す。
2. `Hermit_builtin.cpp` を責務別に分割し、parser/LLM/Boost.Process への直接依存を必要な実装だけに限定する。
3. `test.cpp` を機能別に分割し、クリーンビルドでは `/MP` を有効化する。ただし PDB の競合が起きない設定を維持する。
4. `/GL`・`/LTCG` は無効化せず、リンク入力の過剰さと incremental LTCG の再利用状況を先に調べる。

計測値の完全な一覧は `release-x64-rebuild-2026-11-23.csv`、グラフは `release-x64-rebuild-2026-11-23.svg` を参照する。

## 再計測（2026-11-23、分割後）

### 計測方法の標準

ボトルネック調査は、Visual Studio 18 の同一 Developer Command Prompt／PTY、Release x64、`/t:Rebuild`、`/m:1`、`_CL_=/Bt+` で再現する。`c1xx.dll` をフロントエンド、`c2.dll` をバックエンドとして翻訳単位別に集計し、LTCG リンクと IPDB/IOBJ 再利用はコンパイル時間と分離する。比較時はツールセット、構成、並列度、依存復元状態を揃え、ログ・CSV・SVG・終了コードを同じ計測単位へ保存する。

同じ Visual Studio 18 環境・Release x64・`/t:Rebuild /m:1`・`_CL_=/Bt+` で再計測した。フルビルドは **177.14 秒**で成功した。詳細は `release-x64-rebuild-2026-11-23-rerun.csv`、上位翻訳単位のグラフは `release-x64-rebuild-2026-11-23-rerun.svg`、生ログは `release-x64-rebuild-2026-11-23-rerun.log` に保存した。

変更前のボトルネックは `Hermit_parser.cpp` のフロントエンド **37.03 秒**、`Hermit_parser_visitors.cpp` **17.32 秒**だった。parser と visitor が共有実装ヘッダーを解析していた。2026-11-27 の分割後計測では両者の c1xx 合計が54.34秒から32.31秒へ約40.6%短縮した。詳細は後述の parser template boundary 計測記録を参照。

LTCG は Hermit アプリで **53,688 関数**、テストでも IPDB が存在せず全再コンパイルとなった。従ってリンク入力の過剰さだけでなく、再利用可能な IPDB/IOBJ がないフルリビルド条件自体がリンク時間の主要因である。`test` Release x64 では `UseLibraryDependencyInputs=false` を明示し、HermitLib 静的ライブラリを入力とする構成を維持している。

parser 境界試作後の HermitLib 単独 `/t:Rebuild /m:1` 計測では、`Hermit_parser.cpp` が **35.52 秒**（フロントエンド 35.11 秒、コード生成 0.42 秒）、`Hermit_parser_visitors.cpp` が **15.75 秒**（フロントエンド 15.68 秒、コード生成 0.07 秒）となった。文法コンストラクター本体の除外により、前回の 37.44 秒／17.38 秒からそれぞれ約 5.1%／9.4% 短縮したが、parser が最大ボトルネックである状態は変わらない。計測ログは `hermitlib-boundary-rerun.log` に保存した。

## 方針再整理（2026-11-23計測時点、2026-11-27追記前）

最新のソリューションフルリビルドは **193.90 秒**。最大の翻訳単位は `Hermit_parser.cpp` **35.62 秒**、次いで `Hermit_llm.cpp` **15.47 秒**、`Hermit_parser_visitors.cpp` **14.92 秒**、`Hermit_cu.cpp` **13.81 秒**、`Hermit_sb.cpp` **13.43 秒**、`Hermit_sd.cpp` **11.83 秒**、`test/test.cpp` **14.25 秒**だった。型消去 API と ComfyUI adapter の追加で HTTP 境界は整理できたが、Boost.Beast を含む adapter 自体のコンパイル時間も発生している。

優先順位を次のように更新する。

1. parser の Spirit 文法・visitor テンプレートを別々の実装境界へ分割する（2026-11-27に実施、詳細は後述）。
2. `Hermit_llm.cpp` と `Hermit_cu.cpp` から Beast 実装依存を adapter `.cpp` へ移し、公開／backend ヘッダーの再解析を抑える。
3. test の公開 API 契約テストを軽量ヘッダー単位へ分離し、`test_public_api.cpp` の Beast 依存を削減する。
4. LTCG は維持し、IPDB/IOBJ 再利用とライブラリ入力を測定する。

今回の計測では `Hermit_public_http_adapter.cpp` に MSVC C4267（size_t から unsigned への変換）警告が出たため、次回は警告を解消してから再計測する。

警告修正後の再計測では、Release x64 フルリビルドは **191.27 秒**で成功した。`Hermit_parser.cpp` はフロントエンド **35.31 秒**、`Hermit_parser_visitors.cpp` は **14.70 秒**、`Hermit_llm.cpp` は **14.69 秒**、`Hermit_cu.cpp` は約 **13.8 秒**だった。修正後ログは `release-x64-rebuild-warning-fixed.log` に保存した。parser が引き続き第一優先であり、adapter 警告は解消済みである。

字句 rule component 移行後の HermitLib 単独 `/Bt+` 再計測も成功した。`Hermit_parser.cpp` はフロントエンド **43.95 秒**、コード生成 **0.57 秒**、`Hermit_parser_visitors.cpp` はフロントエンド **18.19 秒**、コード生成 **0.13 秒**だった。ログは `hermitlib-lexical-component.log` に保存した。単独再ビルドは他の翻訳単位と同時実行されるため、絶対値はソリューション計測と直接比較せず、同一条件での前後比較に用いる。

assignment／document component 移行後の再計測では、`Hermit_parser.cpp` はフロントエンド **34.69 秒**、コード生成 **0.41 秒**、`Hermit_parser_visitors.cpp` はフロントエンド **14.37 秒**、コード生成 **0.08 秒**だった。ログは `hermitlib-assignment-component.log` に保存した。parser 本体は前回の字句 component 単独計測より短縮し、カテゴリ分割後も最大ボトルネックであることを確認した。

公開 parser ヘッダーから grammar／component 定義を内部ヘッダーへ移した後の Hermit.sln フルリビルドは成功した。`Hermit_parser.cpp` は `/Bt+` のフロントエンド **43.92 秒**、コード生成 **0.47 秒**、`Hermit_parser_visitors.cpp` は **17.57 秒**だった。リンクでは IPDB/IOBJ が再利用できず **53,649 関数**を全再コンパイルした。ログは `full-rebuild-opaque-parser-2026-11-23.log` に保存した。全テストは **60/60** 成功した。

## 再計測（2026-11-27、parser grammar/visitor 分割後）

Visual Studio 18、Release x64、solution `/t:Rebuild /m:1`、`_CL_=/Bt+` でフルリビルドし、5分02.94秒、警告0・エラー0で成功した。parser c1xx は37.02576秒から19.04035秒、visitor c1xx は17.31892秒から13.26766秒となり、合計で約40.6%短縮した。c2 backend合計は0.47699秒から0.55573秒へ増加し、効果は主にfrontend解析時間の短縮である。

旧共有ヘッダーを `Hermit_parser_grammar_impl.hpp` と `Hermit_parser_visitor_impl.hpp` に分け、非テンプレートの `Hermit_parser_evaluation_internal.hpp` で接続した。ログ、比較CSV、グラフは `release-x64-parser-boundary-2026-11-26.log`、`parser-template-boundary-2026-11-27.csv`、`parser-template-boundary-2026-11-27.svg` に保存する。既定Release x64は105/105 tests、Stage 8実験構成は128/128 tests成功。
