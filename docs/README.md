# 資料ガイド

Hermit の資料を目的別に案内します。内容の正本がある資料は、他の文書へ詳細を重複して記載せず、相互リンクで参照します。

## 要求・課題・仕様

| 資料 | 役割 |
| --- | --- |
| [REQUIREMENTS.md](REQUIREMENTS.md) | ユーザー承認済みの宣言的要求の正本。要求本文の追加・変更はユーザーの明示的な指示に基づきます。 |
| [ISSUES.md](ISSUES.md) | 実装上の課題、優先度、状態、解決記録の正本。 |
| [SPECIFICATION.md](SPECIFICATION.md) | 現行実装の技術仕様、ファイル構成、依存関係、CLI と設定の詳細。 |
| [../readme.md](../readme.md) | 利用者向けの導入、基本的な使い方、実行例。詳細な仕様は技術仕様書を参照します。 |

## API・実装計画・知見

| 資料 | 役割 |
| --- | --- |
| [BACKEND_WEB_API_COVERAGE.md](BACKEND_WEB_API_COVERAGE.md) | REQ-005 の対象バックエンド API を操作単位で棚卸しし、公式根拠、実装、テスト、未対応範囲を対応付ける台帳。 |
| [MACRO_PARSER_REDEVELOPMENT.md](MACRO_PARSER_REDEVELOPMENT.md) | マクロ parser の再開発方針、段階、公開構文との境界。 |
| [PARSER_TEMPLATE_BOUNDARY.md](PARSER_TEMPLATE_BOUNDARY.md) | parser/visitor のテンプレート境界分割と計測結果。 |
| [DEVELOPMENT_KNOWLEDGE.md](DEVELOPMENT_KNOWLEDGE.md) | 実装・検証から得た再利用可能な知見と根拠。 |
| [IDENTIFIERS.md](IDENTIFIERS.md) | 資料間で参照する固定 ID の規則と台帳。 |
| [../build-profile/README.md](../build-profile/README.md) | ビルド計測の手順、比較結果、関連ログの案内。 |

## 更新の流れ

- 要求への追加・変更は、ユーザーの明示的な指示に基づいて [REQUIREMENTS.md](REQUIREMENTS.md) を更新します。
- 課題の追加、状態変更、解決結果は [ISSUES.md](ISSUES.md) に記録します。
- 公開 CLI、設定、入出力の変更時は [../readme.md](../readme.md) と [SPECIFICATION.md](SPECIFICATION.md) を実装に合わせて更新します。
- API の対応状況は [BACKEND_WEB_API_COVERAGE.md](BACKEND_WEB_API_COVERAGE.md)、計測の再現可能な結果は `build-profile/` の資料を正本として維持します。
