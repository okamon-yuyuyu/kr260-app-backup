# application概要
## aruco_auto_go
arucoを見ながら閾値まで前進するプロジェクト<br>
arucoの性能評価が目的<br>
推奨bit :　?

## aruco_data_autoset
プレ実験で使用したプロジェクト<br>
aruco_data_collectionに、ワイヤ初期調整が同一プロジェクト上で実施できるようにしたもの<br>
推奨bit : peri_AXI_control_auto_wrapper.bit

## aruco_data_collection
プレ実験の基となったプロジェクト<br>
手動モードと自動モードを選択する<br>
手動モードでは1~9の姿勢と前進を選ぶ、その際のコマンドをtxtで保存<br>
自動モードでは保存したコマンドのtxtファイルを上から順番に再生する<br>
どちらのモードも判断ごとに画像とその時のエンコーダ値を保存<br>
推奨bit : peristalsis_AXI_control_wrapper.bit

## aruco_test
ArUcoが正しく機能しているかを確認するプロジェクト<br>
x,y,zとroll,pitch,yawを出力する(DisplayPort接続推奨)<br>
PLは不要、PSにWebカメラがつながれていたらOK

## camera_calibration
Webカメラをキャリブレーションするための画像を集めるプロジェクト<br>
Enterを押したらその問いの画像を取得できる<br>
PL不要、PSにWebカメラを繋げばOK

## peri_AXI_collect
本番用データ収集プロジェクト<br>
aruco_data_autosetにdeadzone調整機能とエンコーダ生値(12bit)保存機能を追加した<br>
推奨bit : peristalsis_AXI_runtime_wrapper.bit

## peri_AXI_commamd
蠕動運動をAXIの通信で実行(PSでコマンドを入力、PLでその動きを実行)<br>
蠕動する直前にカメラで撮影を行う<br>
推奨bit : peristalsis_AXI_command_wrapper.bit

## peri_AXI_receiver_encoder
PLのスティックの動きをPSで確認するためのプロジェクト<br>
推奨bit : peri_AXI_receiver_encoder_wrapper.bit

## peri_AXI_RW
可動域やストロークを自由にPSで変更できるプロジェクト<br>
ロボットの動作確認に有効<br>
推奨bit : peri_AXI_RW_wrapper.bit

## python
ホストPCで実行したpythonの処理と同様の動きがPSでできるのかを検証するためのプロジェクト<br>
PLは不要

## trash 
削除予定のものを収納

## wire setting
ワイヤを初期調整するためのプロジェクト<br>
推奨bit : wire_setting_wrapper.bit
