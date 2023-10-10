#ifndef ARDUINO_ARCH_SPRESENSE
#error "Board selection is wrong!!"
#endif
#ifdef SUBCORE
#error "Core selection is wrong!!"
#endif

#include <Camera.h>
#include <DNNRT.h>
#include <SDHCI.h>
#include <RTC.h>
#include <GNSS.h>
#include <OneKeySynthesizerFilter.h>
#include <SFZSink.h>

// TODO クラスにしたい...こんなフラグ作りたくないよう...
bool doPlaySound = false;  // sound再生を実行しているかどうか True = sound再生中 false = sound停止中
bool inRing = false;       // ユーザーの足がリング内に存在しているかどうか True = 入っている false = 入っていない
bool doJump = false;       // ユーザーがジャンプをしたかどうか True = ジャンプをした false = ジャンプをしていない
uint32_t soundPlayTime; // TODO変えるかも

const uint16_t DNN_WIDTH = 96;
const uint16_t DNN_HEIGHT = 96;
const uint8_t PLAY_CHANNEL = 1;

// TODOグローバル変数やっぱりクラスして色々いい感じにしたいよう...
DNNRT dnnrt;
DNNVariable input(DNN_WIDTH *DNN_HEIGHT);

static SFZSink sink("SawLpf.sfz");
OneKeySynthesizerFilter *inst;


/* 音楽再生処理 */
void playSound() {
  if (!doPlaySound) {
    doPlaySound = true;
    soundPlayTime = RTC.getTime().unixtime();
    printf("soundPlayTime is %d\n", soundPlayTime);
    inst->sendNoteOn(OneKeySynthesizerFilter::NOTE_ALL, DEFAULT_VELOCITY, PLAY_CHANNEL);
  }
  uint32_t elapsedTime = RTC.getTime().unixtime() - soundPlayTime;
  printf("time %d\n", elapsedTime);
  if (doPlaySound && elapsedTime >= 2 /*サウンド起動からの経過時間(s)*/) {
    doPlaySound = false;
    inst->sendNoteOff(OneKeySynthesizerFilter::NOTE_ALL, DEFAULT_VELOCITY, PLAY_CHANNEL);
  }
  inst->update();
}

/**
 * ★　この処理は必ずカメラの初期処理の前に呼び出すこと。
 *呼び出しをしない場合はカメラの初期処理となぜか競合して落ちる。
 */
void initSound() {
  inst = new OneKeySynthesizerFilter("twinkle-little-star.mid", sink);
  // setup instrument
  if (!inst->begin()) {
    Serial.println("ERROR: init error.");
    while (true) {
      delay(1000);
    }
  }
  // この処理は音楽再生を実行するが初回呼び出しをしておかないと一部楽譜データ？みたいなものがメモリに展開されない
  // この処理はカメラの初期処理の前に呼び出す必要がある。はずだけど。。。なんかカメラつけているとサウンドがバグリがちでよくわからない
  // またloop中に呼ばれると初回に少し時間がかかるためラグを防ぐためでもある。
  inst->sendNoteOn(OneKeySynthesizerFilter::NOTE_ALL, DEFAULT_VELOCITY, PLAY_CHANNEL);
  inst->sendNoteOff(OneKeySynthesizerFilter::NOTE_ALL, DEFAULT_VELOCITY, PLAY_CHANNEL);
}


/* カメラ　*/
void initCamera() {
  //bufferは持たない　pictureのタイミングは自力で制御するため
  auto came_err = theCamera.begin(0, 0, 0, CAM_IMAGE_PIX_FMT_NONE, 0);
  printf("Camera begin No = %d\n", came_err);

  came_err = theCamera.setStillPictureImageFormat(DNN_WIDTH, DNN_HEIGHT, CAM_IMAGE_PIX_FMT_GRAY);
  printf("Camera set Still Picture Image Format  No = %d\n", came_err);
}

CamImage takePictureWhenMuted() {
  // サウンド再生がされているときには何もしない　サウンドとカメラの処理が競合すると音が壊れるため。
  if (doPlaySound) return;

  return theCamera.takePicture();
}

/* 画像分類（AI）　*/
void initDnnrt() {
  SDClass SD;
  printf("Open SD card");
  File nnbfile = SD.open("model.nnb");
  // 学習済モデルでDNNRTを開始
  auto dnnrt_err = dnnrt.begin(nnbfile);
  printf("Bnnrt begin No is =  %d\n", dnnrt_err);
}

bool isInRing(const CamImage &img) {
  // 推論の実行
  dnnrt.inputVariable(img.getImgBuff(), 0);
  dnnrt.forward();
  DNNVariable output = dnnrt.outputVariable(0);
  int index = output.maxIndex();
  printf("dnnrt index is: %d\n", index);

  bool isInRing = (index == 1) ? true : false;
  return isInRing;
}

void setup() {
  Serial.begin(115200);
  printf("Initialize start\n");
  RTC.begin(); 
  initSound();
  initDnnrt();
  initCamera();
  printf("Initialize completed\n");
}

void loop() {
  auto img = takePictureWhenMuted();
  if (img.isAvailable()) {
    printf("takePicture %d\n", img.getImgBuffSize());
    inRing = isInRing(img);
    /*★ TODO triggerは加速度センサーも追加する必要がある*/
  }
}