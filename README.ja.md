# WeatherBar

Windows の従来型タスクバーに、小さな天気表示。

[简体中文](README.md) | [English](README.en.md) | **日本語**

**現在のバージョン：1.0** · [最初の正式リリース](https://github.com/zdxxm10/WeatherBar/releases/tag/v1.0) · [WeatherBar.exe をダウンロード](https://github.com/zdxxm10/WeatherBar/releases/download/v1.0/WeatherBar.exe)

## インストール

[Releases](https://github.com/zdxxm10/WeatherBar/releases/latest) から `WeatherBar.exe` をダウンロードし、書き込み可能な専用フォルダーで実行します。DLL とアイコンは内蔵しています。Windows のツールバー表示確認で許可するか、タスクバーの「ツールバー」から WeatherBar を選びます。現在のユーザーの登録と自動起動を設定します。

公開版は x64 Windows 10 の従来型タスクバー向けです。Windows 11 標準の新しいタスクバーは DeskBand に非対応です。その他の従来型タスクバー実装と32ビット環境でのインストールは未検証です。

リリースページでは EXE のみをダウンロードしてください。DLL・アイコン・ライセンスを内蔵しています。SHA-256 はリリースノートに記載し、検証用・ライセンス用 TXT の追加ダウンロードは不要です。ソースのライセンスは末尾をご覧ください。

## 設定

天気バーを右クリックして設定を開きます。Open-Meteo `/v1/forecast` の完全な API URL を指定します。hourly に `temperature_2m,weather_code`、daily に `temperature_2m_max,temperature_2m_min,sunrise,sunset`、`timeformat=unixtime` を含めてください。URL のタイムゾーンと整数の UTC オフセットを合わせます。夏時間は手動調整が必要です。初期地点は北京東城区（UTC+8）です。

外観と言語はすぐにプレビューへ反映します。色ボタンでパレットを開き、色見本・RGB スライダー・数値で選択できます。他の場所をクリックすると閉じます。URL は行幅で折り返す3行の欄です。設定は最小サイズで開き、拡大できます。小さな画面ではスクロールできます。

**保存**で設定を書き込み、**キャンセル**または閉じる操作で元の設定と予報キャッシュへ戻します。**外観を初期化**は地点・時差・言語を変更しません。入力途中の無効な URL は、外観プレビューを妨げません。地点の入力は短い待ち時間を置いて反映します。

言語は簡体字中国語・英語・日本語です。既定ではシステムに従い、中国語の各地域は簡体字、日本語は日本語、その他は英語です。インストールと削除は設定言語と独立してシステム言語に従います。

予報は現地の0/6/12/18時に、既定15分のずらし時間を加えて更新します（`StaggerSeconds`、0–1800秒）。ツールチップは無効にできます。有効時は最下部に最終更新と状態を表示し、最高・最低、時間別の紫外線指数、風向・風速、相対湿度、体感温度、降水確率を個別に選択できます（既定では未選択）。文字の実測幅に応じて1行に最大2項目を配置し、欠損値は `--` と表示します。変更は即時プレビューされ、キャンセルで元に戻ります。追加データは既存のリクエストと更新周期を共用し、風速は m/s です。更新確認は手動のみで、自動ダウンロードや自動インストールはありません。API が失敗した場合は、このリポジトリの公式最新リリースへのリダイレクトからバージョンを確認します。失敗時は HTTP または WinHTTP コードを表示します。

## 更新と削除

設定を保持して削除 → エクスプローラーの再起動を待つ → 同じフォルダーの EXE を置換 → 再実行します。カスタム SVG は保持し、不足分だけ追加します。旧名の設定も元ファイルを残して移行します。旧版から初めて更新する前は XML と icons をバックアップし、旧 EXE で先に削除してください。

通常の削除は設定・バックアップ・ログ・アイコン・EXE を保持し、停止した DLL・登録・自動起動を削除します。完全削除は設定と icons も削除し、終了後に Windows 標準 PowerShell で EXE と空フォルダーを削除します。無関係なファイルやディレクトリジャンクションの参照先は保持します。PowerShell が組織のポリシーで禁止されている場合、EXE の手動削除が必要になることがあります。エクスプローラーの再起動でフォルダー画面が閉じるため、先に作業を保存してください。

## ビルドとテスト

Visual Studio の C++ デスクトップ開発（v145）と Windows SDK を使用します。プラグインは C++20、ランチャーは C++17。追加 UI フレームワークや旧パッケージは不要です。

```powershell
MSBuild WeatherBar.sln /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
MSBuild tests\RegressionTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
.\tests\x64\Release\RegressionTests.exe
MSBuild tests\LauncherTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
.\tests\x64\LauncherRelease\LauncherTests.exe
```

出力は `x64/Release/WeatherBar.exe`。テストは独立フォルダーで実行し、インストールやエクスプローラー再起動は行いません。設定プレビューは RegressionTests.exe に `--preview-settings ja` を指定します。Win32 のロジックテストも利用できます。実際のタスクバー・自動起動・通信・画面間 DPI は別途検証が必要です。[構成図](docs/architecture.md)と[検証記録](docs/verification.md)を参照してください。

## 出典とライセンス

[Nusudo-junior/WeatherDeskBand](https://github.com/Nusudo-junior/WeatherDeskBand) に基づき、原作者の [MIT ライセンス](LICENSE) を保持しています。天気：[Open-Meteo](https://open-meteo.com/)。アイコン：Bas Milius の [Meteocons](https://github.com/basmilius/meteocons)、[MIT](src/icons/METEOCONS-LICENSE.txt)。[不具合報告](https://github.com/zdxxm10/WeatherBar/issues)。
