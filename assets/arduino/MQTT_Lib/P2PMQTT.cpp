/*
 * P2PMQTT protocol implementation for Arduino
 * 15.04.2020: byte* -> byte[n]
 */

#include "P2PMQTT.h"

// Accessory descriptor. It's how Arduino identifies itself to Android.
char applicationName[] = "default"; // the app on your phone
char accessoryName[] = "wrox_aoa_accessory"; // your Arduino board
char companyName[] = "Wiley";
char versionNumber[] = "1.0";
char serialNumber[] = "1";
char url[] = "http://media.wiley.com"; // this will have to be corrected

  // Initialize the accessory.
 AndroidAccessory aac(companyName, applicationName, accessoryName, versionNumber, url, serialNumber);

// constructor
P2PMQTT::P2PMQTT(bool debug)
{
  P2PMQTT::keepAliveTimer = 0;
  P2PMQTT::topic = "\0          "; // this creates an empty string, 10 chars long
  P2PMQTT::msgIdOut = 0;
  P2PMQTT::msgIdIn = 0;
  P2PMQTT::isConnectedObject = false;
  P2PMQTT::debug = debug;
  if (P2PMQTT::debug) {
  	Serial.println("P2PMQTT constructed");
  }
}

bool P2PMQTT::begin(const char* model) {
  aac.model = model;
  return aac.begin();
}

bool begin(   const char *manufacturer, const char *model, const char *description,
              const char *version, const char *uri, const char *serial ) {
  aac.manufacturer = manufacturer;
  aac.model = model;
  aac.description = description;
  aac.version = version;
  aac.uri = uri;
  aac.serial = serial;
  return aac.begin();
}

bool P2PMQTT::isConnected() {
  return aac.isConnected();
}

int P2PMQTT::available() {
  return aac.available();
}

int P2PMQTT::read() {
  return aac.read();
}

int P2PMQTT::peek() {
  return aac.peek();
}

size_t P2PMQTT::write(uint8_t *buff, size_t len)
{
  return aac.write(buff, len);
  Serial.println("P2PMQTT::write ok");
}

size_t P2PMQTT::write(uint8_t c) {
  return aac.write(&c, 1);
}


void P2PMQTT::publish(P2PMQTTpublish pub) {  // write pub to aac
  byte msg[14];
  msg[0] = pub.fixedHeader;
  msg[1] = pub.length;
  msg[2] = pub.lengthTopicMSB;
  msg[3] = pub.lengthTopicLSB;
  msg[4] = pub.topic[0];
  msg[5] = pub.topic[1];  
  for (int i=0; i< min(8, pub.length-4); i++) {
	  msg[6+i] = pub.payload[i];
  }
  aac.write(msg, pub.length+2);
  debugB(msg, "P2PMQTT::publish");
}

bool P2PMQTT::checkTopic(byte* buffer, int type, char* topic) {
  byte bufData[8];
  bool result = false;
  int tLen = 0;

  switch(type) {
    case SUBSCRIBE:
	  int tLen = buffer[5];
      for(int i = 0; i < min(8, tLen); i++) {
         bufData[i] = buffer[6+i];
         }
	  result = P2PMQTT::cmpStr(bufData, topic, tLen);	 
      if(result) Serial.println("P2PMQTT::checkTopic ok"); 
	        else debugB(bufData, "P2PMQTT::checkTopic false: " + (String)topic);
      break;
    default:
      break;
  }
  return result;
}

// packs the Payload as a byte array
byte* P2PMQTT::getPayload(byte* buffer, int type) {
	if(debug) {
		Serial.println("P2PMQTT::getPayload, buffer vor getPayload:");
		for(int m = 0; m < 14; m++) { Serial.print(buffer[m], HEX); Serial.print(" "); }; Serial.println();
    }	
//  byte * bufDataOut;
//  bufDataOut = new byte[16];
	  static byte bufDataOut[160];
  switch(type) {
    case PUBLISH:
	  int bufLen = buffer [1];
	  int topicLen = buffer[3];
	  int payloadLen = bufLen - topicLen - 2;
	  for(int i=0; i < payloadLen; i++) {
		  bufDataOut[i] = buffer[i + 6];
	  }
      break;
    default:
      break;
  }
  return bufDataOut;
}

// read next message in the buffer
// assume it is properly formatted
int P2PMQTT::getType(byte* buffer) {
  int length = 0; int totalLength = 0; 
  int msb = 0; int 
  lsb = 0; 
  int valUsb = 0;
  int index = 0; 
  int aux = 0;
  volatile int firstByte = 0; // the first byte seems to break otherwise
  byte* protocolName;
  byte* clientId;
  byte* payload;
  byte* topic_us;
  byte* topic_s;
  byte* topic;
  
  if (aac.isConnected()) {

    if(aac.available() > 0) {
      valUsb = aac.read();   // +++++++++++++++++   1. byte
      firstByte = valUsb;
      int firstByteMSB = valUsb >> 4 & 0x0F;
      if(debug) {
        Serial.print("P2PMQTT::getType, MSG: ");
        Serial.println(firstByteMSB);
      }
      switch (firstByteMSB) {
        case PINGREQ:          
          P2PMQTTpingreq pingreq; // declare the object to store the whole message
          pingreq.fixedHeader = firstByte;  // FIXED HEADER: first byte, message type + flags
          
          while(aac.available() <= 0) {};
          totalLength = aac.read(); // FIXED HEADER: Length
          pingreq.length = totalLength;
          // VARIABLE HEADER: NONE!
          if(debug) { 
            Serial.println("** Ping Request **");
            Serial.println("** FIXED HEADER");
            Serial.print("MSG: "); Serial.println(pingreq.fixedHeader >> 4 & 0x0F);
            Serial.print("Flags: "); Serial.println(pingreq.fixedHeader & 0x0F, BIN);
            Serial.print("Length: "); Serial.println(pingreq.length);
          }         
          index = 0;  // update the buffer
          buffer[index++] = pingreq.fixedHeader;
          buffer[index++] = pingreq.length;
          if(debug) { // activate the following line for low level debugging            
            //for(int m = 0; m < index; m++) { Serial.print(buffer[m], HEX); Serial.print(" "); }; Serial.println();
          }
          break;

        case CONNECT:
          P2PMQTTconnect connect;  // declare the object to store the whole message         
          connect.fixedHeader = firstByte; // FIXED HEADER: first byte, message type + flags        
          while(aac.available() <= 0) {};
          totalLength = aac.read();  // FIXED HEADER: Length
          connect.length = totalLength;
          
          while(aac.available() < 2) {};
          msb = aac.read();  // VARIABLE HEADER: protocol length
		  lsb = aac.read(); 
          length = msb*256 + lsb;
          connect.lengthProtocolNameMSB = msb;
          connect.lengthProtocolNameLSB = lsb;         
          while(aac.available() < length) {};
          protocolName = new byte[length];  // VARIABLE HEADER: protocol name
		  for (int i=0; i<length; i++) {
            protocolName[i] = (byte) aac.read();
		  }
          connect.protocolName = protocolName;
          
          while(aac.available() <= 0) {};
          valUsb = aac.read();  // VARIABLE HEADER: protocol version
          connect.protocolVersion = valUsb;          
          while(aac.available() <= 0) {};
          valUsb = aac.read();  // VARIABLE HEADER: connect flags
          connect.connectFlags = valUsb;
          
          while(aac.available() < 2) {};
          msb = aac.read(); // VARIABLE HEADER: keep alive time
		  lsb = aac.read();
          length = msb*256 + lsb;
          connect.keepAliveMSB = msb;
          connect.keepAliveLSB = lsb;
          
          while(aac.available() < 2) {};
          msb = aac.read(); // VARIABLE HEADER: length client ID
		  lsb = aac.read();
          length = msb*256 + lsb;
          connect.clientIdMSB = msb;
          connect.clientIdLSB = lsb;
          
          clientId = new byte[length]; // VARIABLE HEADER: client ID
		  for (int i=0; i<length; i++) {
            clientId[i] = (byte) aac.read();
		  }
          connect.clientId = clientId;

          // if debugging, print things to the Serial port in a nice way
          if(debug) {
            Serial.println("** P2PMQTT::getType Connect **");
            Serial.println("** FIXED HEADER");
            Serial.print("MSG: "); Serial.println(connect.fixedHeader >> 4 & 0x0F);
            Serial.print("Flags: "); Serial.println(connect.fixedHeader & 0x0F, BIN);
            Serial.print("Length: "); Serial.println(connect.length);
            Serial.println("** VARIABLE HEADER");
            aux = connect.lengthProtocolNameMSB*256 + connect.lengthProtocolNameLSB;
            Serial.print("Length Protocol: "); Serial.println(aux);
            Serial.print("Protocol: "); for(int i = 0; i < aux; i++) Serial.write(connect.protocolName[i]); Serial.println();
            Serial.print("Protocol Version: "); Serial.println(connect.protocolVersion);
            Serial.print("Connect Flags: "); Serial.println(connect.connectFlags);
            Serial.print("Keep Alive Time: "); Serial.println(connect.keepAliveMSB*256 + connect.keepAliveLSB);
            aux = connect.clientIdMSB*256 + connect.clientIdLSB;
            Serial.print("Length Client ID: "); Serial.println(aux);
            Serial.print("Client ID: "); for(int i = 0; i < aux; i++) Serial.write(connect.clientId[i]); Serial.println();
          }

          // update the buffer
          index = 0;
          buffer[index++] = connect.fixedHeader;
          buffer[index++] = connect.length;
          buffer[index++] = connect.lengthProtocolNameMSB;
          buffer[index++] = connect.lengthProtocolNameLSB;
          aux = connect.lengthProtocolNameMSB*256 + connect.lengthProtocolNameLSB;
          for(int i = 0; i < aux; i++) buffer[i+index] = connect.protocolName[i];
          index+=aux;
          buffer[index++] = connect.protocolVersion;
          buffer[index++] = connect.connectFlags;
          buffer[index++] = connect.keepAliveMSB;
          buffer[index++] = connect.keepAliveLSB;
          aux = connect.clientIdMSB*256 + connect.clientIdLSB;
          for(int i = 0; i < aux; i++) buffer[i+index] = connect.clientId[i];
          index+=aux;

          if(debug) {
            // activate the following line for low level debugging
            //for(int m = 0; m < index; m++) { Serial.print(buffer[m], HEX); Serial.print(" "); }; Serial.println();
          }
          break;

        case PUBLISH:         
          P2PMQTTpublish publish; // declare the object to store the whole message
          publish.fixedHeader = firstByte; // FIXED HEADER: first byte, message type + flags
          
          while(aac.available() <= 0) {};
          totalLength = aac.read(); // FIXED HEADER: Length
          publish.length = totalLength;
          
          while(aac.available() < 2) {};
          msb = aac.read(); // VARIABLE HEADER: topic length
		  lsb = aac.read();
          length = msb*256 + lsb;
          publish.lengthTopicMSB = msb;
          publish.lengthTopicLSB = lsb;
          
          while(aac.available() < length) {};
          topic = new byte[length];  // VARIABLE HEADER: topic name
		  for (int i=0; i<length; i++) {
            topic[i] = (byte) aac.read();
		  }
          publish.topic = topic;

          // PAYLOAD: payload
          length = publish.length - publish.lengthTopicMSB*256 - publish.lengthTopicLSB - 2;
          payload = new byte[length];
		  for (int i=0; i<length; i++) {
            payload[i] = aac.read();
		  }
          publish.payload = payload;

          // if debugging, print things to the Serial port in a nice way
          if(debug) {
            Serial.println("** P2PMQTT::getType Publish **");
            Serial.println("** FIXED HEADER");
            Serial.print("MSG: "); Serial.println(publish.fixedHeader >> 4 & 0x0F);
            Serial.print("Flags: "); Serial.println(publish.fixedHeader & 0x0F, BIN);
            Serial.print("Length: "); Serial.println(publish.length);
            Serial.println("** VARIABLE HEADER");
            aux = publish.lengthTopicMSB*256 + publish.lengthTopicLSB;
            Serial.print("Length Topic: "); Serial.println(aux);
            Serial.print("Topic: "); for(int i = 0; i < aux; i++) Serial.write(publish.topic[i]); Serial.println();
            aux = publish.length - publish.lengthTopicMSB*256 - publish.lengthTopicLSB - 2;
            Serial.print("Length Payload:: "); Serial.println(aux);
            Serial.print("Payload: "); 
			for(int i = 0; i < aux; i++) {
				Serial.print(publish.payload[i]); Serial.print(", ");
			}
			Serial.println();
          }

          // update the buffer
          index = 0;
          buffer[index++] = publish.fixedHeader;
          buffer[index++] = publish.length;
          buffer[index++] = publish.lengthTopicMSB;
          buffer[index++] = publish.lengthTopicLSB;
          aux = publish.lengthTopicMSB*256 + publish.lengthTopicLSB;
          for(int i = 0; i < aux; i++) buffer[i+index] = publish.topic[i];
          index+=aux;
          aux = publish.length - publish.lengthTopicMSB*256 - publish.lengthTopicLSB - 2;
          for(int i = 0; i < aux; i++) buffer[i+index] = publish.payload[i];
          index+=aux;

          if(debug) {
			Serial.println("P2PMQTT::getType, buffer nach publish:");
			for(int m = 0; m < index; m++) { Serial.print(buffer[m], HEX); Serial.print(" "); }; Serial.println();
          }
          break;

        case SUBSCRIBE:          
          P2PMQTTsubscribe subscribe;  // declare the object to store the whole message          
          subscribe.fixedHeader = firstByte; // FIXED HEADER: first byte, message type + flags
          
          while(aac.available() <= 0) {};
          totalLength = aac.read();    // FIXED HEADER: Length
          subscribe.length = totalLength;
          
          while(aac.available() < 2) {};
          msb = aac.read();  // VARIABLE HEADER: message ID
		  lsb = aac.read();    
          subscribe.msgIdMSB = msb;
          subscribe.msgIdLSB = lsb;

          // the payload on the subscription is the remaining
          // in the subscription message. However, as we are
          // implementing a single topic subscription system
          // what remains is the topic length, the name and the QoS
          // VARIABLE HEADER: topic length
          while(aac.available() < 2) {};
          msb = aac.read(); 
		  lsb = aac.read();    
          length = msb*256 + lsb;
          subscribe.lengthTopicMSB = msb;
          subscribe.lengthTopicLSB = lsb;

          // VARIABLE HEADER: topic name
          while(aac.available() < length) {};
          topic_s = new byte[length];
		  for (int i=0; i<length; i++) {
            topic_s[i] = (byte) aac.read();
		  }          
          subscribe.topic = topic_s;

          // VARIABLE HEADER: QoS
          while(aac.available() <= 0) {};
          subscribe.topicQoS = aac.read();

          // if debugging, print things to the Serial port in a nice way
          if(debug) {
            Serial.println("** P2PMQTT::getType Subscribe **");
            Serial.println("** FIXED HEADER");
            Serial.print("MSG: "); Serial.println(subscribe.fixedHeader >> 4 & 0x0F);
            Serial.print("Flags: "); Serial.println(subscribe.fixedHeader & 0x0F, BIN);
            Serial.print("Length: "); Serial.println(subscribe.length);
            Serial.println("** VARIABLE HEADER");
            aux = subscribe.msgIdMSB*256 + subscribe.msgIdLSB;
            Serial.print("Msg ID: "); Serial.println(aux);
            aux = subscribe.lengthTopicMSB*256 + subscribe.lengthTopicLSB;
            Serial.print("Length Topic: "); Serial.println(aux);
            Serial.print("Topic: "); for(int i = 0; i < aux; i++) Serial.write(subscribe.topic[i]); Serial.println();
            Serial.print("QoS: "); Serial.println(subscribe.topicQoS);
            Serial.println("P2PMQTT::getType ok");
          }

          // update the buffer
          index = 0;
          buffer[index++] = subscribe.fixedHeader;
          buffer[index++] = subscribe.length;
          buffer[index++] = subscribe.msgIdMSB;
          buffer[index++] = subscribe.msgIdLSB;
          buffer[index++] = subscribe.lengthTopicMSB;
          buffer[index++] = subscribe.lengthTopicLSB;
          aux = subscribe.lengthTopicMSB*256 + subscribe.lengthTopicLSB;
          for(int i = 0; i < aux; i++) buffer[i+index] = subscribe.topic[i];
          index+=aux;
          buffer[index++] = subscribe.topicQoS;

          if(debug) {
            // activate the following line for low level debugging
            // Serial.println("P2PMQTT::getType, buffer nach subscribe:");
            // for(int m = 0; m < index; m++) { Serial.print(buffer[m], HEX); Serial.print(" "); }; Serial.println();
          }
          break;

        case UNSUBSCRIBE:
          // declare the object to store the whole message
          P2PMQTTunsubscribe unsubscribe;

          // FIXED HEADER: first byte, message type + flags
          unsubscribe.fixedHeader = firstByte;

          // FIXED HEADER: Length
          while(aac.available() <= 0) {};
          totalLength = aac.read();
          unsubscribe.length = totalLength;

          // VARIABLE HEADER: message ID
          while(aac.available() < 2) {};
          msb = aac.read(); lsb = aac.read();
          unsubscribe.msgIdMSB = msb;
          unsubscribe.msgIdLSB = lsb;

          // the payload on the subscription is the remaining
          // in the subscription message. However, as we are
          // implementing a single topic subscription system
          // what remains is the topic legnth, the name and the QoS
          // VARIABLE HEADER: topic length
          while(aac.available() < 2) {};
          msb = aac.read(); lsb = aac.read();
          length = msb*256 + lsb;
          unsubscribe.lengthTopicMSB = msb;
          unsubscribe.lengthTopicLSB = lsb;

          // VARIABLE HEADER: topic name
          while(aac.available() < length) {};
          topic_us = new byte[length+1];
          while(length) {
            valUsb = aac.read();
            topic_us[sizeof(topic_us)-length] = (byte) valUsb;
            length--;
          }
		  topic_us[sizeof(topic_us)] = 0;
          unsubscribe.topic = topic_us;

          // if debugging, print things to the Serial port in a nice way
          if(debug) {
            Serial.println("** Unsubscribe **");
            Serial.println("** FIXED HEADER");
            Serial.print("MSG: "); Serial.println(unsubscribe.fixedHeader >> 4 & 0x0F);
            Serial.print("Flags: "); Serial.println(unsubscribe.fixedHeader & 0x0F, BIN);
            Serial.print("Length: "); Serial.println(unsubscribe.length);
            Serial.println("** VARIABLE HEADER");
            aux = unsubscribe.msgIdMSB*256 + unsubscribe.msgIdLSB;
            Serial.print("Msg ID: "); Serial.println(aux);
            aux = unsubscribe.lengthTopicMSB*256 + unsubscribe.lengthTopicLSB;
            Serial.print("Length Topic: "); Serial.println(aux);
            Serial.print("Topic: "); for(int i = 0; i < aux; i++) Serial.write(unsubscribe.topic[i]); Serial.println();
            Serial.println("** VARIABLE HEADER");

          }

          // update the buffer
          index = 0;
          buffer[index++] = unsubscribe.fixedHeader;
          buffer[index++] = unsubscribe.length;
          buffer[index++] = unsubscribe.msgIdMSB;
          buffer[index++] = unsubscribe.msgIdLSB;
          buffer[index++] = unsubscribe.lengthTopicMSB;
          buffer[index++] = unsubscribe.lengthTopicLSB;
          aux = unsubscribe.lengthTopicMSB*256 + unsubscribe.lengthTopicLSB;
          for(int i = 0; i < aux; i++) buffer[i+index] = unsubscribe.topic[i];
          index+=aux;

          if(debug) {
            // activate the following line for low level debugging
            //for(int m = 0; m < index; m++) { Serial.print(buffer[m], HEX); Serial.print(" "); }; Serial.println();
          }
          break;

        default:
          Serial.println("got nothing I can decypher");
          break;
      }
      return firstByteMSB;
   }
  }
  return -1;
}

/* UTILS */
bool P2PMQTT::cmpStr(byte* str1, char* str2, int length) {
  bool result = false;
  for(int i = 0; i < length; i++) {
    result |= (str1[i] == str2[i]);
  }
  return result; // bs 27.12.15
}

byte* P2PMQTT::getTopicUSB(int length) {
  byte topic[length];
  while(length) {
    int valUsb = aac.read();
    topic[sizeof(topic)-length] = (char) valUsb;
    length--;
  }
  return topic;
}

void P2PMQTT::debugB(byte* b, String msg) {
	if (debug) {
	  String s = msg + ": ";
	  for (int i=0; i< sizeof(b); i++) {
		s += String(b[i]) + ", ";  
	  } 
	Serial.println(s);	
	}
}
