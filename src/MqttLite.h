#ifndef MQTT_LITE_H
#define MQTT_LITE_H

#include <stdint.h>
#include <functional>
#include "Client.h"

/**
	MQTT lite main class.
*/
class MqttLite {
	public:
		/**
			MQTT client states.
		*/
		typedef enum {
			STATE_CONNECTION_TIMEOUT         = -4,
			STATE_CONNECTION_LOST            = -3,
			STATE_CONNECT_FAILED             = -2,
			STATE_DISCONNECTED               = -1,
			STATE_CONNECTED                  = 0,
			STATE_CONNECT_BAD_PROTOCOL       = 1,
			STATE_CONNECT_BAD_CLIENT_ID      = 2,
			STATE_CONNECT_UNAVAILABLE        = 3,
			STATE_CONNECT_BAD_CREDENTIALS    = 4,
			STATE_CONNECT_UNAUTHORIZED       = 5
		} state_t;

		/**
			MQTT packet types.
		*/
		typedef enum {
			PACKET_TYPE_CONNECT             = 1,
			PACKET_TYPE_CONNECT_ACK         = 2,
			PACKET_TYPE_PUBLISH             = 3,
			PACKET_TYPE_PUBLISH_ACK         = 4,
			PACKET_TYPE_PUBLISH_RECEIVED    = 5,
			PACKET_TYPE_PUBLISH_RELEASE     = 6,
			PACKET_TYPE_PUBLISH_COMPLETE    = 7,
			PACKET_TYPE_SUBSCRIBE           = 8,
			PACKET_TYPE_SUBSCRIBE_ACK       = 9,
			PACKET_TYPE_UNSUBSCRIBE         = 10,
			PACKET_TYPE_UNSUBSCRIBE_ACK     = 11,
			PACKET_TYPE_PING_REQUEST        = 12,
			PACKET_TYPE_PING_RESPONSE       = 13,
			PACKET_TYPE_DISCONNECT          = 14
		} packet_type_t;

		/**
			Qualities of service.
		*/
		typedef enum {
			QOS0 = 0,
			QOS1 = 1,
			QOS2 = 2
		} qos_t;

		/**
			Callback signature to listen to incomming PUBLISH packets.

			@param p_topic			Topic name.
			@param p_payload		Payload.
			@param p_length			Payload length.
		*/
		typedef std::function<void(const char* p_topic, const uint8_t* p_payload, uint32_t p_length)> publish_callback_t;

		/**
			Callback signature to listen to incomming raw packets.

			Warning: only packets not natively handled will be advertised (this excludes PUBLISH, PINGREQ and PINGRESP).

			@param p_type		Packet type.
			@param p_flags		Flags.
			@param p_length		Remaining length.
			@param p_data		Packet data.
		*/
		typedef std::function<void(packet_type_t p_type, uint8_t p_flags, uint32_t p_length, const uint8_t* p_data)> raw_callback_t;

	private:
		Client* m_client;
		publish_callback_t m_publishCallback;
		raw_callback_t m_rawCallback;
		uint32_t m_bufferSize;
		uint32_t m_timeout;
		uint32_t m_keepAlive;
		state_t m_state;
		uint8_t* m_buffer;
		uint8_t* m_bufferData;
		uint32_t m_lastIn;
		uint32_t m_lastOut;
		bool m_pongReceived;
		uint16_t m_messageId;

	public:
		/**
			Alternative constructor without PUBLISH nor raw packet callback.

			@see `MqttLite(Client*, publish_callback_t, raw_callback_t, uint32_t, uint16_t, uint16_t)`
		*/
		MqttLite(Client* p_client, uint32_t p_bufferSize = 128, uint16_t p_timeout = 15, uint16_t p_keepAlive = 15);

		/**
			Alternative constructor without raw packet callback.

			@see `MqttLite(Client*, callback_t, uint32_t, uint16_t, uint16_t)`
		*/
		MqttLite(
			Client* p_client,
			publish_callback_t p_publishCallback,
			uint32_t p_bufferSize = 128,
			uint16_t p_timeout = 15,
			uint16_t p_keepAlive = 15
		);

		/**
			Constructs a new MQTT client.

			@param p_client					Client.
			@param p_publishCallback		Incomming PUBLISH packet callback (0 to disable).
			@param p_rawCallback			Incomming raw packet callback (0 to disable).
			@param p_bufferSize				Internal buffer size. Any message (input or output) larger that this value will be rejected.
			@param p_timeout				Read timeout (in seconds).
			@param p_keepAlive				Keep alive interval (in seconds). Ping packets will be send after this amount of time by the `loop()` if no other I/O operation occurs.
		*/
		MqttLite(
			Client* p_client,
			publish_callback_t p_publishCallback,
			raw_callback_t p_rawCallback,
			uint32_t p_bufferSize = 128,
			uint16_t p_timeout = 15,
			uint16_t p_keepAlive = 15
		);

		/**
			Destructor.
		*/
		~MqttLite();

		/**
			Returns the client state.
		*/
		inline state_t state() const;

		/**
			Returns whether the client is connected or not (TCP session established and MQTT handshake done).

			@returns	The connection state.
		*/
		bool connected();

		/**
			Alternative connection method without will message.

			@see `connect(const char*, const char*, const char*, const char*, const char*, bool, qos_t, bool)`
		*/
		bool connect(const char* p_id, const char* p_user = 0, const char* p_password = 0, bool p_cleanSession = false);

		/**
			Connects to the MQTT server.

			@param p_id					Client id.
			@param p_user				User (0 to disable it).
			@param p_password			Password (0 to disable it).
			@param p_willTopic			Will topic (0 to disable it).
			@param p_willMessage		Will message (0 to disable it).
			@param p_willRetain			Retain strategy for the will message.
			@param p_willQos			QOS for the will message.
			@param p_cleanSession		Whether to clean the session or not.
			@returns					Whether the connection succeeded or not.
		*/
		bool connect(
			const char* p_id,
			const char* p_user,
			const char* p_password,
			const char* p_willTopic,
			const char* p_willMessage,
			bool p_willRetain,
			qos_t p_willQos,
			bool p_cleanSession = false
		);

		/**
			Alternative publish method with a `\0` ending string payload.

			@see `publish(const char*, const uint8_t*, uint32_t, bool, qos_t, bool)`
		*/
		uint16_t publish(const char* p_topic, const char* p_payload, bool p_retain = false, qos_t p_qos = QOS0, uint16_t p_dupMessageId = 0);

		/**
			Publishes a message to the provided topic.

			@param p_topic				Topic name.
			@param p_payload			Payload to publish.
			@param p_length				Payload length (in bytes).
			@param p_retain				Retain strategy for this message.
			@param p_qos				QOS for this message.
			@param p_dup				Whether this message is a duplicate or not.
			@param p_dupMessageId		If set (> 0), set the DUP flag and uses this value as message id.
			@returns					0 on error, 1 for QOS0 success and the message id for QOS1/2 success.
		*/
		uint16_t publish(
			const char* p_topic,
			const uint8_t* p_payload,
			uint32_t p_length,
			bool p_retain = false,
			qos_t p_qos = QOS0,
			uint16_t p_dupMessageId = 0
		);

		/**
			Subscribes to the provided topic.

			@param p_topic				Topic name.
			@param p_qos				Maximum QOS requested for messages of this subscription.
			@param p_dupMessageId		If set (> 0), set the DUP flag and uses this value as message id.
			@returns					0 on error or the message id on success.
		*/
		uint16_t subscribe(const char* p_topic, qos_t p_qos = QOS0, uint16_t p_dupMessageId = 0);

		/**
			Unsubscribes from the provided topic.

			@param p_topic				Topic name.
			@param p_dupMessageId		If set (> 0), set the DUP flag and uses this value as message id.
			@returns					0 on error or the message id on success.
		*/
		uint16_t unsubscribe(const char* p_topic, uint16_t p_dupMessageId = 0);

		/**
			Processes incomming packets and fires PING requests accordingly.

			Note: For persistent connections, this method must be called frequently to ensure the input buffer doesn't overflow.

			@returns		Processing state (`false` in case of errors).
		*/
		bool loop();

		/**
			Disconnects from the server.
		*/
		void disconnect();

	private:
		uint16_t writeString(uint32_t p_position, const char* p_value);
		bool writePacket(packet_type_t p_type, uint32_t p_length, bool p_retain = false, qos_t p_qos = QOS0, bool p_dup = false);

		bool readByte(uint32_t* p_length);
		bool readPacket(uint32_t* p_length);
};

MqttLite::state_t MqttLite::state() const {
	return m_state;
}

#endif
