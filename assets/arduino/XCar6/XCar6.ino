/*
   Stand 13.06.20, bigblack
   - XCar6
   + Brushless Motor sensor

!! Bilbliothek unter C:\dev\dev\arduino\libraries\MQTT_Lib !!!
!! Please do not use Digital pin 7 as input or output because is used in the comunication with MAX3421E)
*/

#include <Servo.h>
#include <P2PMQTT.h>
#include<Wire.h>

//Pins
#define motorPin    11  // YELLOW
#define servoPin    10 // GREEN
#define interruptPin      2  // for die ISR

// car type
#define carTypeSeben  1
#define carTypeCrawl  2
byte carType = 0;

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
byte* payload;  // def aus switch rausnemhemen, wegen crossing initialisation

// Kommandos
int plId, plCmd, plT, plR, plV, plS, plA; // Payload, kommt von ganz oben, Zeit, Kurvenradius, Geschw., Umdrehungsmessung, Winkel
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

boolean doCountRots = false;
int rots = 0;  // Umdrehungen je Kommando, wenn plS=1
volatile bool sensorVal = false;  // ISR2
int rpm = 0; // rotations per meter, wird als init-Parameter plS mitgegeben
int rotMax = 0;

// Gyro
boolean doCountGyro = false;
long dipsPerGrad = 0; // wird bei init gesetzt
long dipsMax = 0;    // anzahl der Impulse for 1 Step
long dips = 0;
const int MPU_addr=0x68;  // I2C address of the MPU-6050
int16_t AcX,AcY,AcZ,Tmp,GyX,GyY,GyZ;
long gyT, gyDur, gyP;

boolean subscribed = false; // interessiert sich jemand
boolean missionFinish = false;

// drive
Servo lenkungServo;
Servo motorServo;  // weil das geht auch ueber pwm
int motorServoVal = 90;

// los gehts
P2PMQTT mqtt(true); // add parameter true for debugging
P2PMQTTpublish pub;

void setup() {
  Serial.begin(9600);
  Serial.println("init...");
  lenkungServo.attach(servoPin);
  motorServo.attach(motorPin);
  doWait();  // erst mal beide Motoren auf Stillstand
  delay(5000);  // Zeit zwischen einpluggen und "Start"

  mqtt.begin("XCar6");  // --> P2PMQTT -> AndroidAccessory -> MAX3421E::powerOn()
  if (mqtt.isConnected()) {  // AndroidAccessory.isConnected() -> UsbHost...()
       logge (" +++ init connected");
  } else {
       logge (" ### init connected failure");
  }

  attachInterrupt(digitalPinToInterrupt(interruptPin), ISR2, RISING);

  Wire.begin();
  Wire.beginTransmission(MPU_addr);
  Wire.write(0x6B);  // PWR_MGMT_1 register
  Wire.write(0);     // set to zero (wakes up the MPU-6050)
  Wire.endTransmission(true);

  Serial.println("start");
  tStart = millis();
}  // end setup()

void loop() {

  if (missionFinish) {
    delay(100);
    goto loopEnd;
    }
  loopStart = millis();

  // Umdrehung GLS count?
  if (doCountRots) {
    if (sensorVal == true) {
      rots++;
      sensorVal = false;
    }
  }

// 1. gibt's was von oben?
  type = mqtt.getType(mqtt.buffer);
  if (type < 0) { goto loopEnd; }

// 2. Auswerten des Pakets
  switch(type) {

  case CONNECT:
     logge (" getType: connect");
     break;

  case SUBSCRIBE:  // die da oben deuten an, dass sie sich interessieren
//  char* myTopic = char[2];
 // MQTT_TOPIC.toCharArray(myTopic, 2);
  
    subscribed = mqtt.checkTopic(mqtt.buffer, type, MQTT_TOPIC);  // der Form halber... check "AN"
    if (subscribed) {
      logge (" getType: subscribed");
    } else {
      logge(" getType: ??? not subscribed");
    }
    break;

  case PUBLISH:  // das kommt vom Android
      logge (" getType: publish");
      payload = mqtt.getPayload(mqtt.buffer, type);
      for (int k=0; k<7; k++) { Serial.print(payload[k]); Serial.print(", "); } Serial.println();
      plId  = (unsigned int) payload[0];
      plCmd = (unsigned int) payload[1];
      plT = (unsigned int) payload[2];  // in sek., bei cmdInit: 101/102: Seben / Crawler
      plR = (unsigned int) payload[3];  // 0..15 , 8==geradeaus, 0: voll rechts, 15: voll links
      plV = (unsigned int) payload[4];  // -- bei carTypeSeben::  1..15 , 8==halt, 0: voll speed back, 15: voll speed vor
      plS = (unsigned int) payload[5];  // -- bei carTypeCrawl::  Weg in Meter fuer das MOVE
      plA = (unsigned int) payload[6];  // -- bei Gyro: Winkel in 30 Grad (12 == Ein Vollkreis)

      tStart = millis();
      logge("-> got id.cmd.TRVSA  " + String(plId) + " / " + String(plCmd) + " / " + String(plT) + " / " + String(plR) + " / " + String(plV) + " / " + String(plS) + " / " + String(plA));

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
           if (plT == 101 || plT == 102) {
             carType = plT - 100;  // wir kriegen 101 / 102
             rpm = plS;
             dipsPerGrad = 128 * plR + plV;
             doPublish0(rcOk, carType);
             logge(" ---> I N I T   mit carType = " + String(plT) + ", rpm= " + String(rpm) + ", dPG = " + String(dipsPerGrad));
           } else {
             cancel(" #### cmdInit: falsche plT --> ERROR.. " + String(plT), 17);
           }
          goto loopEnd;
          break;

        case cmdMove:
          if (carType == 0) {
            cancel(" #### cmdMove vor cmdInit --> ERROR", 2);
          } else {
            if (plS != 0) {  // Way length gegeben
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

            doMove();
//            logge(" -> MOVE dur = " + String(duration) + ", cnt: " + String(rotMax) + ", dips: " + String(dipsMax));
          }
          break;

        case cmdWait:
          logge(" ---> WAIT dur = " + String(duration));
          doWait();
          break;

        case cmdStop:
          logge(" ---> S T O P");
          doPublish0(rcOk, 0);
          doWait();
          missionFinish = true;
          break;

      } // switch cmd

    break;  //   case PUBLISH

  default:         // do nothing
	  logge(" ### falsches CMD ###");
    break;
  } // switch mqtt.type

loopEnd:
    if (iAmActive && (plT > 0) && (millis() - tStart > duration) ) {  // Zeit abgelaufen ?
      String s = (doCountRots) ? String(rots) : ";";
      logge("CMD done in time ..." + s);
      doPublish0(rcOk, 0);
      // sende OK ans Android, und warte auf neues Kommando.
    }

    if (iAmActive && doCountRots && (rotMax < rots) ) {   // test auf Weglaenge GLS
      logge("CMD done in way ..." + String(rots));
      doPublish0(rcOk, 0);
    }

    if (iAmActive && doCountGyro) {
      dips += getGyroDips();
      if (abs(dips) >= dipsMax) {
        logge("CMD done, Angle reached... , dips = " + String(dips));
        doPublish0(rcOk, 0);
      }
    }

} // end loop

void doMove() {
   int v0 = 0;
   if (carType == carTypeSeben) {
     v0 = plV;     // 1..15
     if (v0 == 0) v0 = 8;  // goldene Mitte --> auf 90 gemappt
     v0 = 16 - v0;   // sonst macht er backward
     motorServoVal = map(v0, 1, 15, 20, 160);

     if (motorServoVal > 90 ) { // backward
       if (! backw) { // reverse
         logge(" # # # # backwards");
         backw = true;
         motorServo.write(180);
         delay(100);
         motorServo.write(90);
         delay(100);
       }
     } else {
       backw = false;
     }
   } else { // crawl
     v0 = 16 - plV;
     motorServoVal = map(v0, 1, 15, 20, 160);  // Startwert
   }
   motorServo.write(motorServoVal);

   int r0 = plR;
   if (r0 == 0) r0 = 8;
   int lenkungServoVal = map(r0, 1, 15, 20, 160);
   lenkungServo.write(lenkungServoVal); // brumm brumm....
   logge(" +++ MOVE mit v/r: " + String(motorServoVal) + " / " + String(lenkungServoVal) + " :: " + String(plV) + " / " + String(plR));
} // doMove()

void doWait () {
  logge(" + + + Stay + + + ");
  lenkungServo.write(90);
  motorServoVal = 90;
  motorServo.write(motorServoVal);
}

void doPublish(int rc, byte* val)  {  //  wir sind rum
  if (mqtt.isConnected() && subscribed) {
    byte bval[8];  for(int i=0; i<8; i++) {bval[i] = 0;}
    bval[0] = plId;
    bval[1] = rc;
    for (int i=0; i<min(6, sizeof(val)); i++) {
      bval[i+2] = val[i];
    }
    logge(bval, "doPublish: payload");
    pub.payload = bval;
    pub.fixedHeader = 48;
    pub.length = 4 + 2 + 6;  // 4 + payload
    pub.lengthTopicMSB = 0;
    pub.lengthTopicLSB = 2;  // length of  MQTT_TOPIC
    pub.topic = (byte*) MQTT_TOPIC;  // "AN" s. oben
    mqtt.publish(pub);
    logge("doPublish: published rc/id: " + String(rc) + " bei id: " + plId);
    iAmActive = false;

    doCountGyro = false;
    dips = 0;
    doCountRots = false;
    rots = 0;
  } else {
    logge(" ??? reached but not conn+subsr ??? id/rc:" + String(plId) + " / " + String(rc));
  }
}

void doPublish0(int rc, byte val)  {
    byte b[1];
    b[0] = val;
    doPublish(rc, b);
}

///////////////////////////////////// TOOLS
void cancel(String msg, byte rcVal) {
  logge(msg);
  doWait();
  doPublish0(rcCancel, rcVal);
  missionFinish = true;
}

void logge(String msg) {
  if (msg != lastMsg) {
    lastMsg = msg;
    Serial.println("### " + msg);
  }
}

void logge(byte *b, String msg) {
  String t = msg + ": ";
  for (int i=0; i<sizeof(b); i++) {
     t += (String) b[i] + ", ";
  }  
  Serial.println(t);
}

int sign(long x) {
  if (x<0) return -1;
  if (x>0) return 1;
  return 0;
}

void ISR2() {
  sensorVal = true;
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
//      Serial.print(" | GyX = "); Serial.print(GyX);
//      Serial.print(" | GyY = "); Serial.print(GyY);
  Serial.print(" | GyZ = "); Serial.print(GyZ);  Serial.print(" | p = "); Serial.println(gyP);
  gyT = millis();
  return gyP;
}
