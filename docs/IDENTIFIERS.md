# 参照用項目 ID

この資料は、複数の資料から参照する項目に付ける ID の種類と形式を定義します。作業上の規範はルート `AGENTS.md` に従います。

## ID 形式

| 項目の種類 | 接頭辞・形式 | 正本 |
| --- | --- | --- |
| ユーザー承認済み要求 | `REQ-NNN` | [`REQUIREMENTS.md`](REQUIREMENTS.md) |
| 課題 | `ISS-NNN` | [`ISSUES.md`](ISSUES.md) |
| マクロ parser 導入段階 | `MPR-STG-NNN` | [`MACRO_PARSER_REDEVELOPMENT.md`](MACRO_PARSER_REDEVELOPMENT.md) |
| Qi 属性・文法の知見 | `QI-<対象>-NNN` | [`DEVELOPMENT_KNOWLEDGE.md`](DEVELOPMENT_KNOWLEDGE.md) |
| 評価処理の知見 | `EVAL-<対象>-NNN` | [`DEVELOPMENT_KNOWLEDGE.md`](DEVELOPMENT_KNOWLEDGE.md) |
| 依存性注入の知見 | `DI-<対象>-NNN` | [`DEVELOPMENT_KNOWLEDGE.md`](DEVELOPMENT_KNOWLEDGE.md) |
| ビルドの知見 | `BUILD-<対象>-NNN` | [`DEVELOPMENT_KNOWLEDGE.md`](DEVELOPMENT_KNOWLEDGE.md) |

`NNN` は最低3桁の10進連番です。ID は項目の種類を接頭辞で示し、登録後は固定します。表の並び順、優先度、完了状態を番号で表しません。各資料の正本内で未使用の最大番号の次を割り当て、欠番を再利用しません。外部資料から参照するときは省略せず完全な ID を記載します。

既存 ID の形式変更が必要な場合は、参照箇所を同時に更新し、旧 ID と新 ID の対応を正本に記録します。単なる見出し変更、優先度変更、表の並べ替えを理由に ID を変更しません。

