/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * UdpNet.cpp
 *
 *  Created on: 6 de set de 2016
 *      Author: user
 */

#include "sha256.h"
#include "BU64.h"
#include <Curve25519.h>
#include <ArduinoJson.h>
#include "MysecUtil.h"
#include "MysecUdpNet.h"

void MysecUdpNet::init(int port, bool integraAlarmePar) {
  if (port > 0) {
    MYSECSWITCH_DEBUGF2(F("Begin por:%d\n"), port);
    udpClient.begin(port);
  }
  memset(pb2, 0, 32);
  // 0 -> não tem udp
  // 1 -> integra alarme
  // 2 -> ignora alarme
  estado = integraAlarmePar ? 1 : 2;
}

MysecUdpNet::~MysecUdpNet() {
	udpClient.stop();
}
/**
 * passkey : chave do usuario. pubkey : chave public gerada a partir da chave do usuario
 */
String MysecUdpNet::receive(const uint8_t * passkey, uint64_t deviceId) {
	int cb = udpClient.parsePacket();
	if (cb) {
	  MYSECSWITCH_DEBUGF2(F("1 - cb:%d\n"), cb);
    uint8_t * buffer = new uint8_t[cb];
    udpClient.readBytes(buffer, cb);
    String tPrint; tPrint.reserve(44);
    uint16_t siz = readInt(buffer);
    if (siz == 0) {
      // as mensagens de alarme começam com 00
      MYSECSWITCH_DEBUGLN(F("2 - mensagem de alarme"));
      if (estado != 1) {
        delete[] buffer;
        MYSECSWITCH_DEBUGLN(F("3 - Não estamos aceitando mensagem de alarme"));
        return ""; // nao aceitamos comandos do alarme
      }
      siz = readInt((uint8_t *)(buffer + 2));
      uint8_t * hash = (uint8_t *)(buffer + 4);
      uint32_t val = readLong((uint8_t *)(buffer + 36));
      uint16_t destino = readInt((uint8_t *)buffer + 40);
      uint16_t origem = readInt((uint8_t *)buffer + 42);
      Sha256.initHmac(passkey, 32);
      Sha256.write((uint8_t)((val) & 0xFF));
      Sha256.write((uint8_t)((val >> 8) & 0xFF));
      Sha256.write((uint8_t)((val >> 16) & 0xFF));
      Sha256.write((uint8_t)((val >> 24) & 0xFF));
      Sha256.write((uint8_t)((destino) & 0xFF));
      Sha256.write((uint8_t)((destino >> 8) & 0xFF));
      Sha256.write((uint8_t)((origem) & 0xFF));
      Sha256.write((uint8_t)((origem >> 8) & 0xFF));
      if (siz > 0) {
        tPrint.remove(0);
        BU64::encode(tPrint, buffer + 48, siz);
        MYSECSWITCH_DEBUGF2(F("buffer:%s\n"), tPrint.c_str());
        Sha256.write((buffer + 48), siz);
      }
      uint8_t * hash2 = Sha256.resultHmac();
      tPrint.remove(0);
      BU64::encode(tPrint, hash, 32);
      MYSECSWITCH_DEBUGF2(F("siz:%d val:%d destino:%d origem:%d rechash:%s\n"), siz, val, destino, origem, tPrint.c_str());
      tPrint.remove(0);
      BU64::encode(tPrint, passkey, 32);
      MYSECSWITCH_DEBUGF2(F("userpasskey: %s\n"), tPrint.c_str());
      tPrint.remove(0);
      BU64::encode(tPrint, hash2, 32);
      MYSECSWITCH_DEBUGF2(F("calchash:%s\n"), tPrint.c_str());
      if (memcmp(hash, hash2, 32)) {
        MYSECSWITCH_DEBUGLN(F("hash invalido"));
        return "";
      }
      uint16_t msgid = readInt((uint8_t *)buffer + 44);
      MYSECSWITCH_DEBUGF2(F("Mensagem :%d\n"), msgid);
      // verifica qual eh o comando
      switch (msgid) {
      case MESSAGE_TYPES::MSG_DUMMY:
      case MESSAGE_TYPES::MSG_SWITCHSTATE:
      case MESSAGE_TYPES::MSG_ALARMSTATUS:
      case MESSAGE_TYPES::MSG_PINCHANGE:
        MYSECSWITCH_DEBUGLN(F("Descartada"));
        break;
      case MESSAGE_TYPES::MSG_IMHOME:
        hab = readLong((uint8_t *)buffer + 48);
        nextEventHab = millis() + hab;
        hab = -10; // depois muda para um valor positivo para que o autoswitch fique desabilitado pelo tempo programado
        MYSECSWITCH_DEBUGF2(F("Desabilita automatico por :%d\n"), hab);
        break;
      case MESSAGE_TYPES::MSG_ALARMNOTURNO:
      case MESSAGE_TYPES::MSG_ALARMDISABLED:
        // desabilita programacao automatica
        MYSECSWITCH_DEBUGLN(F("Desabilita automatico"));
        //hab = -1;
        hab = -11; // depois muda para -1 para que o autoswitch fique desabilitado pelo tempo programado
        break;
      case MESSAGE_TYPES::MSG_ALARMENABLED:
        // habilita programacao automatica
        //hab = 0;
        hab = -12; // depois muda para 0 para que o autoswitch fique habilitado
        MYSECSWITCH_DEBUGLN(F("Habilita automatico"));
        break;
      case MESSAGE_TYPES::MSG_ALARMFIRED:
        // acende todas as luzes por 10 minutos
        hab = -2; // -2 => fired, depois muda para -3 para não ficar mostrando mensagem repetida de q é para acender as luzes
        // calcula tempo para deixar ligado
        nextEventHab = millis() + 10 * 60 * 1000;
        MYSECSWITCH_DEBUGLN(F("Acende todas as luzes por 10 minutos"));
        break;
      case MESSAGE_TYPES::MSG_SWITCHTESTFIRED:
        // acende todas as luzes por 1 minuto
        hab = -2; // muda para -3 para não ficar mostrando mensagem repetida de q é para acender as luzes
        nextEventHab = millis() + 1 * 60 * 1000;
        MYSECSWITCH_DEBUGLN(F("Acende todas as luzes por 1 minuto"));
        break;
      }
      delete[] buffer;
    } else {
      String s;
      s.reserve(cb);
      for (int i = 0; i < cb && buffer[i] != 0; i++) {
          s += (char) buffer[i];
      }
      delete[] buffer;
	    int pos = s.indexOf(';');
	    String payload = s.substring(pos + 1);
	    MYSECSWITCH_DEBUGF2(F("Recebido: %s\n"), s.c_str());
	    int fase, des2;
	    {
	      StaticJsonBuffer<1000> jsonBuffer;
	      JsonObject& data = jsonBuffer.parseObject(payload);
	      if (!data.success()) {
	        MYSECSWITCH_DEBUGF2(F("parseObject() failed %s\n"), payload.c_str());
	        return "";
	      }
	      fase = data["fase"];
	      String des1 = data["desafio1"];
	      des2 = data["desafio2"];
	      if (fase == 1) {
	        BU64::decode(pb2, des1.c_str(), 44);
	      }
	    }
	    MYSECSWITCH_DEBUGF2(F("parseObject ok fase %d\n"), fase);
	    if (fase == 1) {
	      memset(sessionKey, 0, 32);
	      // valida a mensagem inicial com HMAC da chave do usuario
	      if (!MysecUtil::validateToken(payload.c_str(), s.c_str(), passkey)) {
	        MYSECSWITCH_DEBUGF2(F("Falhou o hash da mensagem payload:%s token:%s gentoken:%s\n"), payload.c_str(), s.c_str(), MysecUtil::makeToken(payload.c_str(), passkey).c_str());
	        memset(pb2, 0 ,32);
	        return "";
	      }
	      yield();
	      StaticJsonBuffer<1000> jsonBuffer;
	      JsonObject& root = jsonBuffer.createObject();
	      // retorna o ok
	      root["fase"] = 1;
	      root["desafio1"] = des2;
	      root["desafio3"] = random(10,10000);
	      root["desafio4"] = MysecUtil::ulltoa(deviceId);
	      root["s"] = millis();
	      String sendPayload;
	      sendPayload.reserve(root.measureLength()+1);
	      root.printTo(sendPayload);
	      // fase 1 ainda envia com assinatura com a chave do usuário
//	      Sha256.initHmac(passkey, 32); // key, and length of key in bytes
//	      Sha256.print(sendPayload);
//	      hash = Sha256.resultHmac(); // 32 bytes
//	      String sendHash; sendHash.reserve(44);
//	      BU64::encode(sendHash, hash, 32);
	      String sendHash = MysecUtil::makeToken(sendPayload.c_str(), passkey);
	      MYSECSWITCH_DEBUGF2(F("enviando resposta para %s:%d : %s\n"), udpClient.remoteIP().toString().c_str(), udpClient.localPort(), sendPayload.c_str());
	      remote = udpClient.remoteIP();
	      udpClient.beginPacket(udpClient.remoteIP(), udpClient.localPort());
	      MYSECSWITCH_DEBUGF2(F("SendHash:%s\n"), sendHash.c_str());
	      udpClient.print(sendHash);
	      udpClient.print(";");
        MYSECSWITCH_DEBUGF2(F("SendPayload:%s\n"), sendPayload.c_str());
	      udpClient.print(sendPayload);
	      udpClient.endPacket();
        MYSECSWITCH_DEBUGLN(F("Sent"));
	    } else if (fase == 2 && (sessionKey[0] != 0 || sessionKey[1] != 0 || sessionKey[2] != 0)) {
	      // pedido de estado
        yield();
        if (!MysecUtil::validateToken(payload.c_str(), s.c_str(), sessionKey)) {
	        MYSECSWITCH_DEBUGLN(F("Assinatura fase 2 invalida"));
	        return "";
	      }
	      MYSECSWITCH_DEBUGLN(F("Assinatura fase 2 ok"));
	      return payload;
	    } else {
	      MYSECSWITCH_DEBUGLN(F("mensagem em fase nao reconhecida"));
	    }
		}
	}
	return "";
}
bool MysecUdpNet::makeSharedKey(const uint8_t * passkey) {
	if (pb2[0] != 0 || pb2[1] != 0 || pb2[2] != 0) {
    String b; b.reserve(45);
    BU64::encode(b, pb2, 32);
    MYSECSWITCH_DEBUGF2(F("pb2 : %s\n"), b.c_str());
		// gera shared
    uint8_t f[32];
		memcpy(f, passkey, 32);
		f[0] &= 248;
		f[31] &= 127;
		f[31] |= 64;
		memcpy(sessionKey, pb2, 32);
		// já responde usando a sessionkey
		Curve25519::dh2(sessionKey, f);
		//curve25519_donna(sessionKey, pk1, pb2);
		b.remove(0);
		BU64::encode(b, sessionKey, 32);
		MYSECSWITCH_DEBUGF2(F("sessionKey : %s\n"), b.c_str());
		memset(pb2, 0 ,32);
		return true;
	} else {
		return false;
	}
}
void MysecUdpNet::send(String& payload) {
  if (sessionKey[0] != 0 || sessionKey[1] != 0 || sessionKey[2] != 0) {
    MYSECSWITCH_DEBUGF2(F("enviando resposta para %s : %s\n"), remote.toString().c_str(), payload.c_str());
    udpClient.beginPacket(remote, udpClient.localPort());
    udpClient.print(MysecUtil::makeToken(payload.c_str(), sessionKey));
    udpClient.write(';');
    udpClient.print(payload);
    udpClient.endPacket();
  }
}
uint16_t MysecUdpNet::readInt(uint8_t * buffer) {
  return buffer[1] * 256 + buffer[0]; // big enddian
}
uint32_t MysecUdpNet::readLong(uint8_t * buffer) {
  return buffer[3] * 16777216 + buffer[2] * 65536 + buffer[1] * 256 + buffer[0]; // big enddian
}
bool MysecUdpNet::isConfigured() {
  // temos UDP se estado for != 0
  return estado != 0;
}
bool MysecUdpNet::isDesabilitaAutomatico() {
  return estado != 0 && (hab == -1 || // se desabilitado/alarme desativado (-1) ou
      // udpNet->hab != 0 --> menor que -1 é fired, maior que 0 é imhome -- em ambos os casos desabilita por um período
      ((hab != 0) && millis() < nextEventHab));
}
//if ((dev.udpNet->hab == -2 || dev.udpNet->hab == -3) // estamos com alarme disparado, não executar autoswitch. Todos os pinos de output deve estar ligados.
//    || (dev.udpNet->hab == -1 || dev.udpNet->hab == -11) // desabilitado
//    || ((dev.udpNet->hab == 1 || dev.udpNet->hab == -10) && millis() < dev.udpNet->nextEventHab) // im home e ainda não passou o tempo
bool MysecUdpNet::isAlarmDisabled() {
  return hab == -1 || hab == -11;
}
bool MysecUdpNet::isAlarmFired() {
  return hab < -1;
}
bool MysecUdpNet::isAlarmAusent() {
  return hab == 0 || hab == -12;
}
bool MysecUdpNet::isAlarmPresent() {
  return (hab == 1 || hab == -10) && millis() < nextEventHab;
}
bool MysecUdpNet::isEventExpired() {
  return millis() >= nextEventHab;
}
uint32_t MysecUdpNet::getNextEventHab() {
  return nextEventHab;
}
void MysecUdpNet::setNextEventHab(uint32_t next) {
  nextEventHab = next;
}
MysecUdpNet _mysecUdpNet;