package com.example.xcar8;
// Stand 30.7.24 B

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Message;
import android.os.SystemClock;
import android.speech.tts.TextToSpeech;
import android.text.method.ScrollingMovementMethod;
import android.util.Log;
import android.view.SurfaceView;
import android.view.View;
import android.widget.EditText;
import android.widget.TextView;

import com.example.mqttlib.UsbConnection12;
import com.example.mqttlib.WroxAccessory;
import com.example.xcar8.mission.Mission;
import com.example.xcar8.mission.MissionStep;
import com.example.xcar8.mission.MissionUtils;

import org.opencv.android.BaseLoaderCallback;
import org.opencv.android.CameraBridgeViewBase;
import org.opencv.android.CameraBridgeViewBase.CvCameraViewListener2;
import org.opencv.android.LoaderCallbackInterface;
import org.opencv.android.OpenCVLoader;
import org.opencv.core.CvType;
import org.opencv.core.Mat;
import org.opencv.imgproc.Imgproc;

import java.io.IOException;
import java.util.Locale;
import java.util.Objects;

public class MainActivity extends Activity implements CvCameraViewListener2 {
   private static final String logTAG = "### XCar8: ";
   static TextToSpeech textToSpeech;
   Tools tools;

   // Camera
   private Mat cameraPic;
   private CameraBridgeViewBase mOpenCvCameraView;
   TextView textView;
   TextView myLog;

   private PicAnalyzeProcess picAnalyzeProcess;
   boolean ocvMode = false;

   long duration;
   long t0, t1 = SystemClock.uptimeMillis();

   // MQTT
   private WroxAccessory mAccessory;
   private UsbConnection12 connection;

   final String MQTT_TOPIC = "AN";
   String subscription;
   private int subscriptionId = 0;
   private byte plCmdId = 0;

   EditText missionFileName;
   Mission myMission = null;
   //   String sFileName1 = "/mnt/sdcard/misc/X2_Seben.txt";  // Handy
   String sFileName1 = Environment.getExternalStorageDirectory().getPath() + "/misc/XCar8.txt";  // Handy
   //   String sFileName1 = "/mnt/sdcard2/misc/X1_Crawl.txt";   // Tablet

   final String handyModel = Build.MODEL + " / " + Build.HARDWARE + " / " +
         Build.TYPE + " / " + Build.PRODUCT + " / " + Build.BRAND + " / " + Build.DEVICE;
   //   Android SDK built for x86 / ranchu / userdebug / sdk_google_phone_x86 / Android / generic_x86
//   AOSP on IA Emulator / ranchu / user / sdk_gphone_x86_arm / google / generic_x86_arm
//   U FEEL LITE / mt6735 / user / P4601AN / WIKO / P4601AN
//   GT-I9100 / smdk4210 / user / GT-I9100 / Samsung / GT-I9100
   boolean isAVD = Build.DEVICE.contains("generic_");

   // Arduino
   protected static final byte ACTION_STOP = 0;
   protected static final byte ACTION_LEFT = 1;
   protected static final byte ACTION_MIDDLE = 2;
   protected static final byte ACTION_RIGHT = 3;
   final static byte CMD_INIT = 1;
   final static byte CMD_MOVE = 2;
   final static byte CMD_WAIT = 3;
   final static byte CMD_STOP = 9;
   final static byte b0 = 0;
   static int oldDir = 0;

   @Override
   protected void onCreate(Bundle savedInstanceState) {
      try {
         super.onCreate(savedInstanceState);
         textToSpeech = new TextToSpeech(
               getApplicationContext(),
               status -> {
                  if (status != TextToSpeech.ERROR) {
                     textToSpeech.setLanguage(Locale.UK);
                  }
               });
         setContentView(R.layout.activity_menu);
         myLog = findViewById(R.id.logText1);
         tools = new Tools(myLog, textToSpeech);
         myLog.setMovementMethod(new ScrollingMovementMethod());
         tools.logge(logTAG, "onCreate beginn");
         missionFileName = findViewById(R.id.MissionFileName);
         missionFileName.setText(sFileName1);
         UsbManager mUsbManager = (UsbManager) getSystemService(USB_SERVICE);
         connection = new UsbConnection12(this, mUsbManager);
         mAccessory = new WroxAccessory(this, myLog);
         plCmdId = 0;
         tools.logge(logTAG, " onCreate ok, model = " + handyModel);
      } catch (Exception e) {
         tools.logge(logTAG, "Error bei onCreate", e);
      }
   }

   @Override
   public void onResume() {
      super.onResume();
      // OpenCV
      if (!OpenCVLoader.initDebug()) {
         Log.d(logTAG, "Internal OpenCV library not found. Using OpenCV Manager for initialization");
         OpenCVLoader.initAsync(OpenCVLoader.OPENCV_VERSION_3_0_0, this, mLoaderCallback);
      } else {
         Log.d(logTAG, "OpenCV library found inside package. Using it!");
         mLoaderCallback.onManagerConnected(LoaderCallbackInterface.SUCCESS);
      }
      // MQTT
      try {
         tools.logge(logTAG, "cmdConnect beginn...  ->  mAccessory.connect()...");
         mAccessory.connect(connection);
         tools.logge(logTAG, "cmdConnect ...  ->  mAccessory.subscribe()...");
         subscription = mAccessory.subscribe(receiver, MQTT_TOPIC, subscriptionId++);
         tools.logge(logTAG, "cmdConnect ende, my subscription is: " + subscription);
      } catch (IOException e) {
         tools.logge(logTAG, "Error: " + e.getMessage());
      }
   }


   //////////////   click methods
   public void cmdTrace(View v) {
      startOcvMode((byte) 0, 25, 35);
   }

   public void cmdForward(View v) {   // wird nur einmal gedrückt, arbeitet die gesamte Mission ab
      try {
         String fileName = missionFileName.getText().toString();
         tools.logge(logTAG, "Forward begin...: " + fileName);
         MissionUtils missionUtils = new MissionUtils();
         myMission = new Mission();
         myMission.setMissionSteps(missionUtils.getMission(fileName, myLog));
         runMissionStep();  // erster Schritt wird angestoßen, der Rest folgt im Loop
         tools.logge(logTAG, "Forward ende");
      } catch (Exception e) {
         tools.logge(logTAG, "Error bei cmdForward", e);
      }
   }

   public void cmdStop(View v) {
      tools.logge(logTAG, "STOP command");
      speakText("command STOP");
      doStop();
   }

   // wird bei cmdForward angestoßen, und vom BroadcastReceiver
   void runMissionStep() {
      byte plCmd; // drive (ggf. auch mit v=0)
      byte plT = 0;  // time
      byte plR = 0;  // radius
      byte plV = 0;  // geschw
      byte plS = 0;  // weg
      byte plA = 0;  // Winkel
      String cmd;

      MissionStep m = myMission.getNextStep();
      if (m == null) {
         tools.logge(logTAG, "run Step = null");
         doStop();
         return;
      }
      ocvMode = false;
      tools.logge(logTAG, "run step: " + myMission.getLine() + ": " + m);
      speakText("run step " + m.getCmd());

      cmd = m.getCmd();

      int x = 0, y = 0; // farbenbereich

      for (int i = 0; i < m.getParamKeys().length; i++) {
         String pKey = m.getParamKeys()[i];
         int iVal = m.getParamVals()[i];
         switch (pKey) {
            case "T":  // bei >= 100 -> millis, sonst sek.
               plT = (byte) iVal;
               break;
            case "R":
               plR = (byte) (iVal + 8); // -7..7 -> 1..15  , keine 0
               break;
            case "V":
               plV = (byte) (iVal + 8); // -7..7 -> 1..15  , keine 0
               break;
            case "S":
               plS = (byte) iVal;
               break;
            case "A":
               plA = (byte) iVal;
               break;
            case "X":
               x = iVal;
               break;
            case "Y":
               y = iVal;
               break;
         }
      }

      switch (cmd) {
         case "INIT":  // == Init für car type
            plCmd = CMD_INIT;
            tools.logge(logTAG, "init: " + plT);
            break;
         case "WAIT":  // == MOVE mit v=0
            plCmd = CMD_WAIT;
            tools.logge(logTAG, "wait: " + plT);
            break;
         case "MOVE":
            plCmd = CMD_MOVE; // drive
            tools.logge(logTAG, "move");
            break;
         case "FIND":
            tools.logge(logTAG, "find");
            startOcvMode(plT, x, y);   // siehe unten
            return;
         default:
            tools.logge(logTAG, "Error bei runMissionStep::Unbekanntes Kommando: " + cmd);
            doStop();
            return;
      }
      sendToCar(plCmd, plT, plR, plV, plS, plA, cmd);
   }

   private void sendToCar(byte plCmd, byte plT, byte plR, byte plV, byte plS, byte plA, String cmd) {

      byte[] buffer = new byte[7]; // ID, cmd, T, R, V, S, A
      if (++plCmdId > 100) plCmdId = 1;

      buffer[0] = plCmdId;
      buffer[1] = plCmd;
      buffer[2] = plT;
      buffer[3] = plR;
      buffer[4] = plV;
      buffer[5] = plS;
      buffer[6] = plA;

      try {
         mAccessory.publish(MQTT_TOPIC, buffer); // ab an den Arduino
      } catch (Exception e) {
         tools.logge(logTAG, "Error bei runMissionStep", e);
         speakText("error at publish");
         doStop();
      }
      myLog.refreshDrawableState();
      tools.logge(logTAG, " -> Published cmd: " + cmd + ", b=" + Tools.b2s(buffer));
   }

   void doStop() {
      try {
         tools.logge(logTAG, "STOP");
         sendToCar(CMD_STOP, b0, b0, b0, b0, b0, "STOP");
         mAccessory.disconnect();
         speakText("do stop and out");
      } catch (IOException e) {
         tools.logge(logTAG, "Error bei doStop", e);  // Exception kam aus mAccessory.publish()...
      }
   }


   // Create the reciever and act on the data, wird an mAccessory.subscribe() gegeben
   private final BroadcastReceiver receiver = new BroadcastReceiver() {
      @Override
      public void onReceive(Context context, Intent intent) {
         try {
            if (!Objects.requireNonNull(intent.getAction()).equalsIgnoreCase(subscription)) {  //"com.wiley.wroxaccessories.SUBSCRIBE.AN"
               tools.logge(logTAG, "onReceive mit falscher Subscription. intent.getAction== " + intent.getAction());
               return;
            }
            byte[] payload = intent.getByteArrayExtra(subscription + ".payload");
            if (payload[0] == 0 && payload.length == 1) {
               tools.logge(logTAG, "onReceive mit leerer Payload? payload[0] = " + payload[0]);
            } else {
               tools.logge(logTAG, "onReceive pl: " + Tools.b2s(payload));
               if (payload[0] != plCmdId) {
                  tools.logge(logTAG, "onReceive plCMD diff, MyID: " + plCmdId);
               }
               switch (payload[1]) {
                  case 1:
                     tools.logge(logTAG, "--> OK");
                     break;
                  case 4:
                     tools.logge(logTAG, "--> TIMEOUT");
                     break;
                  case 8:
                     tools.logge(logTAG, "--> ERROR");
                     break;
                  case 12:
                     tools.logge(logTAG, "--> NO_SINAL -> STOP");
                     doStop();
                     return;
                  case 16:
                     tools.logge(logTAG, "--> CANCEL");
                     doStop();
                     return;
                  default:
                     tools.logge(logTAG, "--> ??? invalid RC " + payload[1]);
                     doStop();
                     return;
               }

               int rcVal = 0;
               if (payload.length > 2) {
                  int k = 1;
                  for (int i = 2; i < payload.length; i++) {
                     rcVal += payload[i] * k;
                     k *= 256;
                  }
                  tools.logge(logTAG, ":: onReceive retVal = " + rcVal + " -> " + Tools.b2s(payload));
               }

               if (payload[1] < 16) {
                  runMissionStep();  // Weiter geht's!
               }
            }
         } catch (Exception e) {
            tools.logge(logTAG, "Error im BroadcastReceiver", e);
         }
      } // onReceive()
   };


   public static void speakText(String tts) {
      // textToSpeech.speak(tts, TextToSpeech.QUEUE_ADD, null, "xxx");
   }

   /////////////////////   Camera + OpenCV
   private void startOcvMode(byte plT, int x, int y) {
      t0 = SystemClock.uptimeMillis();
      duration = plT * 1000;
      ocvMode = true;
      speakText("trace mode started");
      setContentView(R.layout.activity_camera);
      mOpenCvCameraView = findViewById(R.id.t4Camera_view);
      mOpenCvCameraView.setVisibility(SurfaceView.VISIBLE);
      mOpenCvCameraView.setCvCameraViewListener(this);
      textView = findViewById(R.id.textView);
      if (!isAVD) mOpenCvCameraView.setCameraIndex(CameraBridgeViewBase.CAMERA_ID_FRONT);  // Handy
      mOpenCvCameraView.enableView();
      picAnalyzeProcess = new PicAnalyzeProcess(mOpenCvCameraView, this, textView, x, y);
   }

   private void stopOcvMode() {
      ocvMode = false;
      mOpenCvCameraView.setVisibility(SurfaceView.INVISIBLE);
      mOpenCvCameraView.disableView();
      setContentView(R.layout.activity_menu);
      byte[] payload = {plCmdId, 1, 0};  // id, ok, 0
      Intent publishIntent = new Intent();
      publishIntent.setAction(WroxAccessory.SUBSCRIBE + "." + MQTT_TOPIC);
      publishIntent.putExtra(WroxAccessory.SUBSCRIBE + "." + MQTT_TOPIC + ".topic", "AN");  //"com.wiley.wroxaccessories.SUBSCRIBE.AN"
      publishIntent.putExtra(WroxAccessory.SUBSCRIBE + "." + MQTT_TOPIC + ".payload", payload);
      this.sendBroadcast(publishIntent);

   }

   private final BaseLoaderCallback mLoaderCallback = new BaseLoaderCallback(this) {
      @Override
      public void onManagerConnected(int status) {
         if (status == LoaderCallbackInterface.SUCCESS) {
            Log.i(logTAG, "OpenCV loaded successfully");
         } else {
            super.onManagerConnected(status);
         }
      }
   };


   @Override
   public void onCameraViewStarted(int width, int height) {
      cameraPic = new Mat(height, width, CvType.CV_8UC4);
      Log.w(logTAG, "camera dims c/r = " + cameraPic.cols() + " / " + cameraPic.rows());
   }

   @Override
   public void onCameraViewStopped() {
      cameraPic.release();
   }

   @Override
   public Mat onCameraFrame(CameraBridgeViewBase.CvCameraViewFrame inputFrame) {
      cameraPic = inputFrame.rgba();
      if (isAVD) Imgproc.cvtColor(cameraPic, cameraPic, Imgproc.COLOR_BGR2RGB);
      picAnalyzeProcess.setCameraPic(cameraPic);
      picAnalyzeProcess.run(cameraPic);
      if (duration > 0) {
         t1 = SystemClock.uptimeMillis();
         if (t1 - t0 > duration) {
            speakText("trace mode time over");
            tHandler.obtainMessage(1).sendToTarget();
         }
      }
      return cameraPic;
   }

   private  Handler tHandler = new Handler(){
      @Override
      public void handleMessage(Message msg) {
         if (msg.what == 1) {
            stopOcvMode();
         }
      }
   };

   void moveDir(int dir) {
      byte plT = 0;  // time
      byte plV = 3;  // geschw
      byte plR = 0;  // radius, 8:: geradeaus
      byte plS = 0;  // weg
      byte plA = 0;  // Winkel
      int newDir = 0; // 1..3
      switch (dir) {
         case ACTION_LEFT:
            plR = 10;  // radius
            newDir = 1;
            if (oldDir != newDir) {
               speakText("go left");
               Log.i(logTAG, "< < < < < < < < < < < < ");
            }
            break;
         case ACTION_MIDDLE:
            plR = 8;
            newDir = 2;
            if (oldDir != newDir) {
               speakText("go middle");
               Log.i(logTAG, "- - - - - - - - - - - - ");
            }
            break;
         case ACTION_RIGHT:
            plR = 6;  // radius
            newDir = 3;
            if (oldDir != newDir) {
               speakText("go right");
               Log.i(logTAG, "> > > > > > > > > > > > ");
            }
            break;
         case ACTION_STOP:
            plV = 1;
            plR = 8;
            newDir = 2;
            if (oldDir != newDir) {
               speakText("searching");
               Log.i(logTAG, "- - - L O O K - - ");
            }
            break;
      }
      if (oldDir != newDir) {
         sendToCar(CMD_MOVE, plT, plR, plV, plS, plA, "FIND");
         oldDir = newDir;
      }
   }

}
