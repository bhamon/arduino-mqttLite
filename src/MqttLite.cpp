#include <Arduino.h>
#include "MqttLite.h"

#define BUFFER_OFFSET 5
#define MQTT_LEVEL 4

MqttLite::MqttLite(Client* p_client, uint32_t p_bufferSize, uint16_t p_timeout, uint16_t p_keepAlive)
: MqttLite(p_client, 0, p_bufferSize, p_timeout, p_keepAlive) {
}

MqttLite::MqttLite(
	Client* p_client,
	publish_callback_t p_publishCallback,
	uint32_t p_bufferSize,
	uint16_t p_timeout,
	uint16_t p_keepAlive
)
: MqttLite(p_client, p_publishCallback, 0, p_bufferSize, p_timeout, p_keepAlive) {
}

MqttLite::MqttLite(
	Client* p_client,
	publish_callback_t p_publishCallback,
	raw_callback_t p_rawCallback,
	uint32_t p_bufferSize,
	uint16_t p_timeout,
	uint16_t p_keepAlive
)
: m_client(p_client)
, m_publishCallback(p_publishCallback)
, m_rawCallback(p_rawCallback)
, m_bufferSize(p_bufferSize)
, m_timeout(p_timeout * 1000)
, m_keepAlive(p_keepAlive * 1000)
, m_state(STATE_DISCONNECTED) {
	m_buffer = new uint8_t[p_bufferSize];
	m_bufferData = m_buffer + BUFFER_OFFSET;
}

MqttLite::~MqttLite() {
	disconnect();
	delete[] m_buffer;
}

bool MqttLite::connected() {
	if(m_state == STATE_CONNECTED && !m_client->connected()) {
		m_state = STATE_CONNECTION_LOST;
		m_client->stop();
	}

	return m_client->connected() && m_state == STATE_CONNECTED;
}

bool MqttLite::connect(const char* p_id, const char* p_user, const char* p_password, bool p_cleanSession) {
	return connect(p_id, p_user, p_password, 0, 0, false, QOS0, p_cleanSession);
}

bool MqttLite::connect(
	const char* p_id,
	const char* p_user,
	const char* p_password,
	const char* p_willTopic,
	const char* p_willMessage,
	bool p_willRetain,
	MqttLite::qos_t p_willQos,
	bool p_cleanSession
) {
	if(!m_client->connected()) {
		return false;
	}

	uint32_t length = 0;

	m_bufferData[length++] = 0x00;
	m_bufferData[length++] = 0x04;
	m_bufferData[length++] = 'M';
	m_bufferData[length++] = 'Q';
	m_bufferData[length++] = 'T';
	m_bufferData[length++] = 'T';
	m_bufferData[length++] = MQTT_LEVEL;

	uint8_t connectFlags = 0x00;

	if(p_user) {
		connectFlags |= 0b01 << 7;

		if(p_password) {
			connectFlags |= 0b01 << 6;
		}
	}

	if(p_willTopic && p_willMessage) {
		if(p_willRetain) {
			connectFlags |= 0b01 << 5;
		}

		connectFlags |= (p_willQos << 3) & (0b011 << 3);
		connectFlags |= 0b01 << 2;
	}

	if(p_cleanSession) {
		connectFlags |= 0b01 << 1;
	}

	m_bufferData[length++] = connectFlags;
	m_bufferData[length++] = (m_keepAlive / 1000) >> 8;
	m_bufferData[length++] = (m_keepAlive / 1000) & 0x0ff;

	length += writeString(length, p_id);

	if(p_willTopic && p_willMessage) {
		length += writeString(length, p_willTopic);
		length += writeString(length, p_willMessage);
	}

	if(p_user) {
		length += writeString(length, p_user);

		if(p_password) {
			length += writeString(length, p_password);
		}
	}

	if(!writePacket(PACKET_TYPE_CONNECT, length)) {
		m_client->stop();
		m_state = STATE_CONNECT_FAILED;
		return false;
	}

	m_lastIn = millis();
	while(!m_client->available()) {
		uint32_t t = millis();
		if(t - m_lastIn >= m_timeout) {
			m_client->stop();
			m_state = STATE_CONNECTION_TIMEOUT;
			return false;
		}
	}

	if(!readPacket(&length)) {
		m_client->stop();
		m_state = STATE_CONNECT_FAILED;
		return false;
	}

	MqttLite::packet_type_t type = static_cast<MqttLite::packet_type_t>((m_buffer[0] >> 4) & 0x0f);
	if(type != PACKET_TYPE_CONNECT_ACK) {
		m_client->stop();
		m_state = STATE_CONNECT_FAILED;
		return false;
	}

	if(m_bufferData[1] != 0) {
		m_client->stop();
		m_state = static_cast<MqttLite::state_t>(m_bufferData[1]);
		return false;
	}

	m_state = STATE_CONNECTED;
	m_pongReceived = true;
	m_messageId = 0;

	return true;
}

uint16_t MqttLite::publish(const char* p_topic, const char* p_payload, bool p_retain, MqttLite::qos_t p_qos, uint16_t p_dupMessageId) {
	return publish(p_topic, reinterpret_cast<const uint8_t*>(p_payload), strlen(p_payload), p_retain, p_qos, p_dupMessageId);
}

uint16_t MqttLite::publish(
	const char* p_topic,
	const uint8_t* p_payload,
	uint32_t p_length,
	bool p_retain,
	MqttLite::qos_t p_qos,
	uint16_t p_dupMessageId
) {
	if(!connected()) {
		return 0;
	}

	if(BUFFER_OFFSET + 2 + strlen(p_topic) + p_length > m_bufferSize) {
		return 0;
	}

	uint32_t length = 0;

	length += writeString(length, p_topic);

	uint16_t messageId = 1;
	if(p_qos == QOS1 || p_qos == QOS2) {
		if(p_dupMessageId == 0) {
			messageId = ++m_messageId;
		}

		m_bufferData[length++] = messageId >> 8;
		m_bufferData[length++] = messageId & 0x0ff;
	}

	memmove(m_bufferData + length, p_payload, p_length);
	length += p_length;

	if(!writePacket(PACKET_TYPE_PUBLISH, length, p_retain, p_qos, p_dupMessageId > 0)) {
		m_state = STATE_CONNECTION_LOST;
		m_client->stop();
		return 0;
	}

	return messageId;
}

uint16_t MqttLite::subscribe(const char* p_topic, MqttLite::qos_t p_qos, uint16_t p_dupMessageId) {
	if(!connected()) {
		return 0;
	}

	if(BUFFER_OFFSET + 2 + 2 + strlen(p_topic) + 1 > m_bufferSize) {
		return 0;
	}

	uint32_t length = 0;

	uint16_t messageId = p_dupMessageId;
	if(p_dupMessageId == 0) {
		messageId = ++m_messageId;
	}

	m_bufferData[length++] = messageId >> 8;
	m_bufferData[length++] = messageId & 0x0ff;

	length += writeString(length, p_topic);
	m_bufferData[length++] = p_qos;

	if(!writePacket(PACKET_TYPE_SUBSCRIBE, length, false, QOS1, p_dupMessageId > 0)) {
		m_state = STATE_CONNECTION_LOST;
		m_client->stop();
		return 0;
	}

	return messageId;
}

uint16_t MqttLite::unsubscribe(const char* p_topic, uint16_t p_dupMessageId) {
	if(!connected()) {
		return 0;
	}

	if(BUFFER_OFFSET + 2 + 2 + strlen(p_topic) > m_bufferSize) {
		return 0;
	}

	uint32_t length = 0;

	uint16_t messageId = p_dupMessageId;
	if(p_dupMessageId == 0) {
		messageId = ++m_messageId;
	}

	m_bufferData[length++] = messageId >> 8;
	m_bufferData[length++] = messageId & 0x0ff;

	length += writeString(length, p_topic);

	if(!writePacket(PACKET_TYPE_UNSUBSCRIBE, length, false, QOS1, p_dupMessageId > 0)) {
		m_state = STATE_CONNECTION_LOST;
		m_client->stop();
		return 0;
	}

	return messageId;
}

bool MqttLite::loop() {
	if(!connected()) {
		return false;
	}

	unsigned long t = millis();
	if(t - m_lastIn > m_keepAlive || t - m_lastOut > m_keepAlive) {
		if(!m_pongReceived) {
			m_state = STATE_CONNECTION_TIMEOUT;
			m_client->stop();
			return false;
		}

		writePacket(PACKET_TYPE_PING_REQUEST, 0);
		m_lastIn = t;
		m_pongReceived = false;
	}

	if(m_client->available()) {
		uint32_t length;
		if(!readPacket(&length)) {
			m_client->stop();
			m_state = STATE_CONNECTION_LOST;
			return false;
		}

		MqttLite::packet_type_t type = static_cast<MqttLite::packet_type_t>((m_buffer[0] >> 4) & 0x0f);
		switch(type) {
			case PACKET_TYPE_PING_REQUEST:
				writePacket(PACKET_TYPE_PING_RESPONSE, 0);
				break;
			case PACKET_TYPE_PING_RESPONSE:
				m_pongReceived = true;
				break;
			case PACKET_TYPE_PUBLISH:{
				if(!m_publishCallback) {
					break;
				}

				uint16_t topicLength = static_cast<uint16_t>(m_bufferData[0] << 8) | m_bufferData[1];
				char* topic = reinterpret_cast<char*>(m_buffer + BUFFER_OFFSET - 1);
				memmove(topic, m_bufferData + 2, topicLength);
				topic[topicLength] = 0;

				qos_t qos = static_cast<qos_t>((m_buffer[0] >> 1) & 0b011);
				uint8_t offset = 0;
				if(qos != QOS0) {
					offset = 2;
				}

				m_publishCallback(topic, m_bufferData + 2 + topicLength + offset, length - 2 - topicLength - offset);
				break;
			}
			default:
				if(!m_rawCallback) {
					break;
				}

				m_rawCallback(type, m_buffer[0] & 0x0f, length, m_bufferData);
		}
	}

	return true;
}

void MqttLite::disconnect() {
	if(connected()) {
		writePacket(PACKET_TYPE_DISCONNECT, 0);
	}

	m_client->stop();
	m_state = STATE_DISCONNECTED;
}

uint16_t MqttLite::writeString(uint32_t p_position, const char* p_value) {
	uint16_t i = 2;
	while(*p_value) {
		m_bufferData[p_position + i++] = *p_value++;
	}

	m_bufferData[p_position] = (i - 2) >> 8;
	m_bufferData[p_position + 1] = (i - 2) & 0x0ff;

	return i;
}

bool MqttLite::writePacket(MqttLite::packet_type_t p_type, uint32_t p_length, bool p_retain, MqttLite::qos_t p_qos, bool p_dup) {
	uint8_t bufferLength[3];
	uint32_t length = p_length;
	uint8_t index = 0;
	do {
		bufferLength[index] = length & 0x7f;
		length >>= 7;
		if(length) {
			bufferLength[index] |= 0x080;
		}

		++index;
	} while(length);

	uint8_t flags = 0;

	if(p_retain) {
		flags |= 0b01;
	}

	flags |= (p_qos << 1) & (0b011 << 1);

	if(p_dup) {
		flags |= 0b01 << 3;
	}

	uint8_t* packet = m_buffer + BUFFER_OFFSET - index - 1;
	packet[0] = (p_type << 4) | flags;

	memmove(packet + 1, bufferLength, index);

	length = 1 + index + p_length;
	if(m_client->write(packet, length) != length) {
		return false;
	}

	m_lastOut = millis();

	return true;
}

bool MqttLite::readByte(uint32_t* p_length) {
	uint32_t t = millis();
	while(!m_client->available()) {
		t = millis();
		if(t - m_lastIn >= m_timeout) {
			return false;
		}
	}

	m_buffer[(*p_length)++] = m_client->read();
	m_lastIn = t;

	return true;
}

bool MqttLite::readPacket(uint32_t* p_length) {
	uint32_t length = 0;

	if(!readByte(&length)) {
		return false;
	}

	*p_length = 0;
	uint8_t shift = 0;
	do {
		if(!readByte(&length)) {
			return false;
		} else if(shift > 21) {
			return false;
		}

		*p_length += static_cast<uint32_t>(m_buffer[length - 1] & 0x7f) << shift;
		shift += 7;
	} while(m_buffer[length - 1] & 0x080);

	if(BUFFER_OFFSET + *p_length > m_bufferSize) {
		return false;
	}

	length = BUFFER_OFFSET;
	for(uint32_t i = 0 ; i < *p_length ; ++i) {
		if(!readByte(&length)) {
			return false;
		}
	}

	return true;
}
