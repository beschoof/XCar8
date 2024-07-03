/*
1.9.2023
SKYRC: ESC-5V an Vin
!! Bilbliothek unter C:\dev\dev\arduino\libraries\MQTT_Lib 
!! Please do not use Digital pin 7 as input or output because is used in the comunication with MAX3421E)
*/

#include <Servo.h>
#include <P2PMQTT.h>
#include<Wire.h>
#include <LiquidCrystal.h>

//Pins
#define servoPin        9
#define motorPin       10
#define interruptPin    2  // for die ISR

// car type
#define carTypeS   3  // SKYRC
byte carType = 0;
#define mHigh  800
#define mMid  1430
#define mLow  1900

// return codes -> android
#define rcOk       1
#define rcError    8
#define rcCancel  16  // mission cancel

// commands
#define cmdInit  1
#define cmdMove  2
#define cmdWait  3
#define cmdStop  9

// MQTT
byte* payload;  
P2PMQTT mqtt(true); // add parameter true for debugging
P2PMQTTpublish pub;

// Kommandos
int plId, plCmd, plT, plR, plV, plS, plA; // Payload, kommt von ganz oben, Zeit, Kurvenradius, Geschw., Weg, Winkel
int oldId = -1;
char MQTT_TOPIC[] = "AN";
String lastMsg = "";                   // println
int lastGetType = 0;
boolean backw = false;
unsigned long tStart;
unsigned long duration;
unsigned long lfdTime;
unsigned long loopStart;
unsigned long loopTime;
boolean iAmActive = false;
int type = 0;
int rSign = 0; // Richtung des Radius
int b2sOld = 0;

// BLC Sensor
boolean doCountRots = false;
volatile int rots = 0;  // Umdrehungen je Kommando, wenn plS= > 0, ISR2
volatile long t0, t1;
int rpm = 0; // rotations per meter, wird als init-Parameter plS mitgegeben
long rotMax = 0;  // Rotationen, die zum Erreichen der Weglänge PLS erforderlich wären

// Gyro
boolean doCountGyro = false;
long dipsPerGrad = 0; // wird bei init gesetzt
long dipsMax = 0;    // anzahl der Impulse for 1 Step
long dips = 0;
const int MPU_addr=0x68;  // I2C address of the MPU-6050
int16_t AcX,AcY,AcZ,Tmp,GyX,GyY,GyZ;
long gyT, gyDur, gyP;

//display
const int rs = 11, en = 12, d4 = 3, d5 = 4, d6 = 5, d7 = 6;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

boolean subscribed = false; // interessiert sich jemand
boolean missionFinish = false;

// drive
Servo lenkungServo;
Servo motorServo;  // weil das geht auch ueber pwm
int motorServoVal = mMid;

// los gehts
void setup() {
  Serial.begin(9600);
  lcd.begin(16, 2);
  logge("init...");
  lenkungServo.attach(servoPin);
  motorServo.attach(motorPin, 600, 2000);
  attachInterrupt(digitalPinToInterrupt(interruptPin), ISR2, RISING);
  initGyro();
  t1 = millis();
  t0 = t1;
  doWait();  // erst mal beide Motoren auf Stillstand
  delay(5000);  // Zeit zwischen einpluggen und "Start"

  mqtt.begin("xcar8");  // --> P2PMQTT -> AndroidAccessory -> MAX3421E::powerOn()
  if (mqtt.isConnected()) {  // AndroidAccessory.isConnected() -> UsbHost...()
       logge ("++ connected");
  } else {
       logge ("## conn fail");
  }
  attachInterrupt(digitalPinToInterrupt(interruptPin), ISR2, RISING);
  logge("start xcar8");
  tStart = millis();
}  

void loop() {

  if (missionFinish) {
    delay(100);
    goto loopEnd;
    }
  loopStart = millis();

// 1. gibt's was von oben?
  type = mqtt.getType(mqtt.buffer);
  if (type < 0) { goto loopEnd; }

// 2. Auswerten des Pakets
  switch(type) {

  case CONNECT:
     logge ("getType: connect");
     break;

  case SUBSCRIBE:  // die da oben deuten an, dass sie sich interessieren
  //  char* myTopic = char[2];
  // MQTT_TOPIC.toCharArray(myTopic, 2);
  
    subscribed = mqtt.checkTopic(mqtt.buffer, type, MQTT_TOPIC);  // der Form halber... check "AN"
    if (subscribed) {
      logge("getT: subs ok");
    } else {
      logge("getT# subs err");
    }
    break;

  case PUBLISH:  // das kommt vom Android
      payload = mqtt.getPayload(mqtt.buffer, type);
      logge("getT: publish:", payload);
      for (int k=0; k<7; k++) { Serial.print(payload[k]); Serial.print(", "); } Serial.println();
      plId  = (unsigned int) payload[0];
      plCmd = (unsigned int) payload[1];
      plT = (unsigned int) payload[2];  // Zeit in Sek.,                                cmdInit: motorType 101/102/103: Seben / Crawler / BlueMotor  
      plR = (unsigned int) payload[3];  // Radius 1/8/15: right/straight/left,          cmdInit: Gyro: Dips per Grad % 128 (li Byte)
      plV = (unsigned int) payload[4];  // Geschwindigkeit 1..15: back, halt(8), vor    cmdInit: Gyro: Dips per Grad mod 128 (re Byte)
      plS = (unsigned int) payload[5];  // Weg in m                                     cmdInit: Motor-Rotationen per Meter (int rpm)
      plA = (unsigned int) payload[6];  // Winkel in 30 Grad (12 == Ein Vollkreis) bei Gyro

      logge("got id/cmd/TRVSA:", String(plId) + "/" + String(plCmd) + "/" + String(plT) + "/" + String(plR) + "/" + String(plV) + "/" + String(plS) + "/" + String(plA));

      // egal, ob gar kein oder kein neues Kommand kommmt: test auf weiter so
      if (plId == 0 || plCmd == 0 || plId == oldId)  {   // nix neues von oben
        goto loopEnd;
      }

      oldId = plId;
      iAmActive = true;
      duration = plT * 1000; // in ms
      tStart = millis();
      doCountRots = false;
      doCountGyro = false;
      rSign = sign(plR - 8);

      switch(plCmd) {

        case cmdInit:
           if (plT == 101 || plT == 102 || plT == 103) {
             carType = plT - 100;  // wir kriegen 101 / 102 / 103
             if (carType != carTypeS) {
               cancel("# cmdInit: plT err" + String(plT), 11);
             }
             rpm = plS;
             dipsPerGrad = 128 * plR + plV;
             doPublish0(rcOk, carType);
             logge("cmdInit:", String(plT) + "," + String(rpm) + "," + String(dipsPerGrad));
           } else {
             cancel("# cmdInit: plT err", plT);
           }
          goto loopEnd;
          break;

        case cmdMove:
          if (carType == 0) {
            cancel("#### cmdMove vor cmdInit --> ERROR", 2);
          } 
          rotMax = 0; dipsMax = 0; 
          if (plS != 0) {  // way length gegeben
            doCountRots = true;
            rotMax = plS * rpm;
            rots = 0;
          }
          if (plA != 0) {  // Winkel gegeben
            doCountGyro = true;
            dipsMax = plA * dipsPerGrad * 12;
            dips = 0;
            gyT = 0;
          }

          logge("doMOVE " + String(duration) + "," + String(rotMax) + "," + String(dipsMax));
            doMove();          
          break;

        case cmdWait:
          logge("WAIT dur = " + String(duration));
          doWait();
          break;

        case cmdStop:
          logge(" ---> S T O P");
          doPublish0(rcOk, 0);
          doStop();
          missionFinish = true;
          break;

      } // switch cmd

    break;  //   case PUBLISH

  default:         // do nothing
    logge("### wrong CMD", plCmd);
    break;
  } // switch mqtt.type

loopEnd:
    if (iAmActive && (plT > 0) && (millis() - tStart > duration) ) {  // Zeit abgelaufen ?
      String s = (doCountRots) ? String(rots) : ";";
      logge("CMD done in time", rots);
      doPublish0(rcOk, 0);
      // sende OK ans Android, und warte auf neues Kommando.
    }

    if (iAmActive && doCountRots && (rotMax < rots) ) {   // test auf Weglaenge 
      logge("CMD done in way", rots);
      doPublish0(rcOk, 0);
    }

    if (iAmActive && doCountGyro) {  // Winkel erreicht?
      dips += getGyroDips();
      if (abs(dips) >= dipsMax) {
        logge("CMD done in Angle",dips);
        doPublish0(rcOk, 0);
      }
    }

} // end loop

void doMove() {
  String mDir = "++";  // forward
   int v0 = 0;
    if (carType = carTypeS) {
       v0 = 16 - plV;
       motorServoVal = map(v0, 1, 15, mHigh, mLow);
       if (v0 < 8) {        // backward
         mDir = "B ";
         if (! backw) { // reverse
             backw = true;
             mDir = "--";  // backward
             motorServo.write(mHigh); 
             delay(150);
             motorServo.write(mMid); 
             delay(150);
           }
         } else {
           backw = false;
         }      
    } 
   motorServo.write(motorServoVal);

   int r0 = plR;
   if (r0 == 0) r0 = 8;
   int lenkungServoVal = map(r0, 1, 15, 20, 160);
   if (r0 < 8) {
    mDir = mDir + "<<";
   } else if (r0 > 8) {
    mDir = ">>" + mDir;
   } 
  
   lenkungServo.write(lenkungServoVal); 
   logge(mDir + " " + String(motorServoVal) + "/" + String(lenkungServoVal), String(plV) + "/" + String(plR));
} // doMove()

void doWait () {
  logge("++ Stay ++", "wait");
  lenkungServo.write(90);
  motorServoVal = mMid;
  motorServo.write(motorServoVal);
}

void doStop () {
  logge("++ Stop ++", "wait");
  lenkungServo.write(90);
  lenkungServo.detach();
  motorServoVal = mMid;
  motorServo.write(motorServoVal);
  motorServo.detach();
}

void doPublish(int rc, byte* val)  {  //  wir sind rum
  if (mqtt.isConnected() && subscribed) {
    byte bval[8];  for(int i=0; i<8; i++) {bval[i] = 0;}
    bval[0] = plId;
    bval[1] = rc;
    for (int i=0; i<min(6, sizeof(val)); i++) {
      bval[i+2] = val[i];
    }
    pub.payload = bval;
    pub.fixedHeader = 48;
    pub.length = 4 + 2 + 6;  // 4 + payload
    pub.lengthTopicMSB = 0;
    pub.lengthTopicLSB = 2;  // length of  MQTT_TOPIC
    pub.topic = (byte*) MQTT_TOPIC;  // "AN" s. oben
    mqtt.publish(pub);
    logge("doPublish:" + String(rc) + "/" + plId, bval);
    iAmActive = false;

    doCountGyro = false;
    dips = 0;
    doCountRots = false;
    rots = 0;
  } else {  // not mqtt.isConnected() && subscribed ?
    logge("not conn+subscr :" + String(plId) + "/" + String(rc));
  }
}

void doPublish0(int rc, byte val)  {
    byte b[1];
    b[0] = val;
    doPublish(rc, b);
}

///////////////////////////////////// TOOLS
void cancel(String msg, byte rcVal) {
  logge(msg, rcVal);
  doWait();
  doPublish0(rcCancel, rcVal);
  missionFinish = true;
}

void logge(String msg, String msg2) {
  if (msg != lastMsg) {
    lastMsg = msg;
    Serial.println("### " + msg + " / " + msg2);
    lcd.clear();
    lcd.print(msg);
    lcd.setCursor(0, 1);  // andersrum wie in der doku...
    lcd.print(msg2);
  }  
}

void logge(String msg) {
  logge(msg, " ");
}

void logge(String msg, byte *b) {
  String t = "";
  for (int i=0; i<sizeof(b); i++) {
     t += (String) b[i] + ",";
  }
  logge(msg, t);  
}

void logge(String msg, long v) {
  logge(msg, String(v));
}

int sign(long x) {
  if (x<0) return -1;
  if (x>0) return 1;
  return 0;
}

void ISR2() {
  ++rots;
}

/// Gyro
 void initGyro() {
  Wire.begin();
  Wire.beginTransmission(MPU_addr);
  Wire.write(0x6B);  // PWR_MGMT_1 register
  Wire.write(0);     // set to zero (wakes up the MPU-6050)
  Wire.endTransmission(true);
 }

int getGyroDips() {
  if (gyT == 0) gyT = millis();
  Wire.beginTransmission(MPU_addr);
  Wire.write(0x3B);  // starting with register 0x3B (ACCEL_XOUT_H)
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_addr,14,true);  // request a total of 14 registers
  AcX=Wire.read()<<8|Wire.read();  // 0x3B (ACCEL_XOUT_H) & 0x3C (ACCEL_XOUT_L)
  AcY=Wire.read()<<8|Wire.read();  // 0x3D (ACCEL_YOUT_H) & 0x3E (ACCEL_YOUT_L)
  AcZ=Wire.read()<<8|Wire.read();  // 0x3F (ACCEL_ZOUT_H) & 0x40 (ACCEL_ZOUT_L)
  Tmp=Wire.read()<<8|Wire.read();  // 0x41 (TEMP_OUT_H) & 0x42 (TEMP_OUT_L)
  GyX=Wire.read()<<8|Wire.read();  // 0x43 (GYRO_XOUT_H) & 0x44 (GYRO_XOUT_L)
  GyY=Wire.read()<<8|Wire.read();  // 0x45 (GYRO_YOUT_H) & 0x46 (GYRO_YOUT_L)
  GyZ=Wire.read()<<8|Wire.read();  // 0x47 (GYRO_ZOUT_H) & 0x48 (GYRO_ZOUT_L)
  gyDur = millis() - gyT;
  gyP = gyDur * GyZ;
  Serial.print(" | GyX = "); Serial.print(GyX);
  Serial.print(" | GyY = "); Serial.print(GyY);
  Serial.print(" | GyZ = "); Serial.print(GyZ);  Serial.print(" | p = "); Serial.println(gyP);
  gyT = millis();
  return gyP;
}
