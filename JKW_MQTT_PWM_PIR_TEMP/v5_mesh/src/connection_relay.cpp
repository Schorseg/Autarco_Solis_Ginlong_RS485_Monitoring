#include "connection_relay.h"

connection_relay::connection_relay(){
	for (int i = 0; i < ESP8266_NUM_CLIENTS; i++) {
		espClients[i]         = NULL;
		espClientsLastComm[i] = 0;
	}
	espLastcomm       = 0;
	m_AP_running      = false;
	m_connection_type = CONNECTION_DIRECT_CONNECTED; // wifi STA connection can survive reboots
};
connection_relay::~connection_relay(){ };

// scan can return [blocking] the nr of APs
int8_t connection_relay::scan(bool blocking){
	int8_t status;

	if (blocking) {
		// when should we run WiFi.scanDelete();?
		status = WiFi.scanNetworks();
		return status;
	} else {
		status = WiFi.scanComplete();
		if (status == -2) { // scan not triggered
			WiFi.scanNetworks(true);
			return 0;                 // not complete
		} else if (status == -1) { // scan still ongoing
			return 0;                 // not complete
		}
		return status; // complete
	}
	return 0;
}

// we are the last node and should directly connect to the WIFI
bool connection_relay::DirectConnect(){
	logger.println(TOPIC_WIFI, F("Currently not connected, initiate direct connection ..."), COLOR_RED);
	logger.println(TOPIC_WIFI, F("Removing  all old credentials ..."), COLOR_YELLOW);
	// mqtt.nw_ssid, mqtt.nw_pw or autoconnect?
	WiFi.disconnect();
	logger.print(TOPIC_WIFI, F("Connecting to: "), COLOR_YELLOW);
	logger.pln(mqtt.nw_ssid);

	if (wifiManager.connectWifi(mqtt.nw_ssid, mqtt.nw_pw) == WL_CONNECTED) {
		m_connection_type = CONNECTION_DIRECT_CONNECTED;
		return true;
	}
	return false;
}

// direct wifi didn't work, connect via mesh
bool connection_relay::MeshConnect(){
	int8_t scan_count = scan(true); // will start with blocking scan
	int8_t best_RSSI  = -1;

	if (scan_count > 0) {
		// erial.printf("%d network(s) found\r\n", scan_count);
		for (int i = 0; i < scan_count; i++) {
			// erial.printf("%d: %s, Ch:%d (%ddBm) %s %i\n\r", i+1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i) == ENC_TYPE_NONE ? "open" : "", WiFi.isHidden(i));
			// if(WiFi.isHidden(i) && strncmp(WiFi.SSID(i).c_str(),AP_SSID,strlen(AP_SSID))==0){
			if (strncmp(WiFi.SSID(i).c_str(), AP_SSID, strlen(AP_SSID)) == 0) {
				// erial.println("interesting config");
				if (best_RSSI == -1 || WiFi.RSSI(i) > WiFi.RSSI(best_RSSI)) {
					// erial.println("actually the best");
					best_RSSI = i;
				}
			}
		}

		if (best_RSSI > -1) {
			sprintf(m_msg_buffer, "%i Networks found, incl mesh [%idbm]. Connecting", scan_count, WiFi.RSSI(best_RSSI));
			logger.println(TOPIC_WIFI, m_msg_buffer, COLOR_GREEN);

			// connect to AP
			WiFi.persistent(false); // avoids updating the same flash cell every reboot
			WiFi.begin(WiFi.SSID(best_RSSI).c_str(), AP_PW, 0, WiFi.BSSID(best_RSSI), true);
			WiFi.waitForConnectResult();
			if (WiFi.status() == WL_CONNECTED) {
				sprintf(m_msg_buffer, "Mesh connection established %i.%i.%i.%i", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP(
				  )[2], WiFi.localIP()[3]);
				logger.println(TOPIC_WIFI, m_msg_buffer, COLOR_GREEN);
				m_connection_type = CONNECTION_MESH_CONNECTED;
				return true;
			}
		} else {
			sprintf(m_msg_buffer, "No mesh in range");
			logger.print(TOPIC_WIFI, m_msg_buffer, COLOR_RED);
		}
	}
	return false;
} // MeshConnect

// some sort of wifi connection is there, connect to mqtt.
// this can happen directly or via mesh proxy thing
// if mesh connected: Blocking call for server!
bool connection_relay::connectServer(char * dev_short, char * login, char * pw){
	if (m_connection_type == CONNECTION_DIRECT_CONNECTED) {
		sprintf(m_msg_buffer, "Establishing direct MQTT link");
		logger.println(TOPIC_MQTT, m_msg_buffer, COLOR_YELLOW);

		client.setServer(mqtt.server_ip, atoi(mqtt.server_port));
		client.setCallback(callback); // in main.cpp
		return client.connect(dev_short, login, pw);
	} else if (m_connection_type == CONNECTION_MESH_CONNECTED) {
		sprintf(m_msg_buffer, "Establishing indirect MESH-MQTT link");
		logger.println(TOPIC_MQTT, m_msg_buffer, COLOR_YELLOW);

		IPAddress apIP = WiFi.localIP();
		apIP[3] = 1;

		// erial.println("A..");
		// delay(100);

		if (espUplink.connect(apIP, MESH_PORT)) {
			// erial.println("B..");
			// delay(100);
			if (send_up(dev_short, 0)) {
				// erial.println("C..");
				// delay(100);
				sprintf(m_msg_buffer, "Received server welcome");
				logger.println(TOPIC_MQTT, m_msg_buffer, COLOR_GREEN);
			} // only read
			return true;
		}
	}
	return false;
} // connectServer

// suppose to send a raw message around in the network
// second purpose is to start the rounting message which will concat all station on the way
bool connection_relay::loopCheck(){
	if (m_connection_type == CONNECTION_MESH_CONNECTED) {
		sprintf(m_msg_buffer, "sending NETLOOP check up the chain");
		logger.println(TOPIC_CON_REL, m_msg_buffer, COLOR_PURPLE);

		char msg[50];
		uint8_t mac[6];
		WiFi.macAddress(mac);
		sprintf(msg, "%c%02x:%02x:%02x:%02x:%02x:%02x", MSG_TYPE_NW_LOOP_CHECK, mac[5], mac[4], mac[3], mac[2], mac[1],
		  mac[0]);
		enqueue_up(msg, strlen(msg));
		// send_up(msg,strlen(msg));

		// reuse buffer to send a "routing" msg, enqueue already copied last msg
		char topic[50];
		sprintf(topic, "%s", build_topic("routing", UNIT_TO_PC));
		sprintf(msg, "%c%c%c%s%c%s", MSG_TYPE_ROUTING, strlen(topic) + 1, strlen(
		   mqtt.dev_short) + 1, topic, 0, mqtt.dev_short);

		enqueue_up(msg, 3 + strlen(topic) + 1 + strlen(mqtt.dev_short) + 1);
	}
}

// disconnect e.g. to apply new setting before reconnecting
void connection_relay::disconnectServer(){
	if (m_connection_type == CONNECTION_DIRECT_CONNECTED) {
		client.disconnect();
	} else if (m_connection_type == CONNECTION_MESH_CONNECTED) {
		espUplink.stop();
	}
}

// make "connected" more versatile
bool connection_relay::connected(){
	if (m_connection_type == CONNECTION_DIRECT_CONNECTED) {
		return client.connected();
	} else if (m_connection_type == CONNECTION_MESH_CONNECTED) {
		if (!espUplink.connected()) {
			logger.println(TOPIC_CON_REL, F("Disconnected from mesh"), COLOR_RED);
			return false;
		}
		return true;
	}
	logger.println(TOPIC_CON_REL, F("unknown connection type"), COLOR_RED);
	return false;
}

// replacement for the subscribe routine.
// if direct connected simply a mqtt subscribe
// else it will preformat the message
bool connection_relay::subscribe(char * topic){ return subscribe(topic, false); }

bool connection_relay::subscribe(char * topic, bool enqueue){
	if (!connected()) {
		return false;
	}
	if (m_connection_type == CONNECTION_DIRECT_CONNECTED) {
		// erial.print("Direct subscibe: ");
		// erial.println(topic);
		client.subscribe(topic); // MQTT_TRACE_TOPIC topic
		client.loop();
		return client.connected();
	} else if (m_connection_type == CONNECTION_MESH_CONNECTED) {
		uint16_t len = strlen(topic) + 2;
		char msg[len];
		msg[0] = MSG_TYPE_SUBSCRPTION;
		memcpy(msg + 1, topic, strlen(topic));
		msg[strlen(topic) + 1] = '\0';
		// send or enqueue
		if (enqueue) {
			return enqueue_up(msg, len);
		} else {
			return send_up(msg, len);
		}
	}
	return false;
}

// replacement for the publish routine.
// if direct connected simply a mqtt publish
// else it will preformat the message
bool connection_relay::publish(char * topic, char * mqtt_msg){  return publish(topic, mqtt_msg, false); }

bool connection_relay::publish(char * topic, char * mqtt_msg, bool enqueue){
	if (!connected()) {
		return false;
	}
	if (m_connection_type == CONNECTION_DIRECT_CONNECTED) {
		client.publish(topic, mqtt_msg, true);
		client.loop();
		return client.connected();
	} else if (m_connection_type == CONNECTION_MESH_CONNECTED) {
		uint16_t len = strlen(topic) + strlen(mqtt_msg) + 5;
		char msg[len];
		// a bit strange to add 0x00 in the middle of a string, but help to display this later
		sprintf(msg, "%c%c%c%s%c%s", MSG_TYPE_PUBLISH, strlen(topic) + 1, strlen(mqtt_msg) + 1, topic, 0, mqtt_msg); // last 0x00 will be added automatically

		// for(uint8_t i=0; i<strlen(topic)+strlen(mqtt_msg)+5; i++){
		//	.printf("[%i] %c %i\r\n",i,msg[i],msg[i]);
		// }
		// send or enqueue
		if (enqueue) {
			return enqueue_up(msg, len);
		} else {
			return send_up(msg, len);
		}
	}
	return false;
}

// used to send messages from the mqtt server downstream to all connected clients
bool connection_relay::broadcast_publish_down(char * topic, char * mqtt_msg){
	sprintf(m_msg_buffer, "(down) FWD '%s' -> '%s'", topic, mqtt_msg);
	logger.println(TOPIC_CON_REL, m_msg_buffer, COLOR_YELLOW);

	char msg[strlen(topic) + strlen(mqtt_msg) + 4];
	msg[0] = MSG_TYPE_PUBLISH;
	msg[1] = strlen(topic);
	msg[2] = strlen(mqtt_msg);
	memcpy(msg + 3, topic, strlen(topic));
	memcpy(msg + 3 + strlen(topic), mqtt_msg, strlen(mqtt_msg));
	msg[strlen(topic) + strlen(mqtt_msg) + 4 - 1] = '\0';
	for (int i = 0; i < ESP8266_NUM_CLIENTS; i++) {
		if (espClients[i]) {
			espClients[i]->write(msg);
		}
	}
	return true;
}

// send pre-encoded messages (subscriptions/pubish's) UP to our mesh server
bool connection_relay::send_up(char * msg, uint16_t size){
	// erial.println("1..");
	// delay(100);
	if (!connected()) {
		return false;
	}
	uint32_t start = millis();
	uint8_t buf;
	// send up
	// erial.println("2..");
	// delay(100);

	espUplink.write(msg, size);

	// erial.println("3..");
	// delay(100);

	// waitup to 5 sec for ACK
	while ((millis() - start < 5000) || espUplink.available()) {
		// erial.println(millis()-start);
		delay(1); // resets wtd
		if (espUplink.available()) {
			// erial.println("4..");
			// delay(100);
			espUplink.read(&buf, 1);
			// erial.printf("recv %i %c\r\n",buf,buf);
			if (buf == MSG_TYPE_ACK) {
				espLastcomm = millis();
				return true;
			}
		}
	}
	return false;
} // send_up

// add messages to the queueus
bool connection_relay::enqueue_up(char * msg, uint16_t size){
	uint8_t * buf;

	buf = new uint8_t[size + 3];
	if (buf) {
		buf[0] = size / 256;
		buf[1] = size % 256;
		memcpy(buf + 2, msg, size);
		buf[size + 2] = 0x00; // just to be sure
		for (uint8_t i = 0; i < MAX_MSG_QUEUE; i++) {
			if (!outBuf[i]) {
				// erial.print("Enqueue Slot ");
				// erial.println(i);
				// delay(100);
				outBuf[i] = buf;
				return true;
			}
		}
	}
	logger.println(TOPIC_CON_REL, F("Send queue overflow"), COLOR_RED);
	return false;
}

// check if there are messages to send (previously needed due to async server, now flowcontroll)
// send one message at the time (still fast)
bool connection_relay::dequeue_up(){
	uint32_t start;
	uint16_t size;
	uint8_t buf;

	for (uint8_t i = 0; i < MAX_MSG_QUEUE; i++) {
		if (outBuf[i]) {
			size = outBuf[i][0] * 256 + outBuf[i][1]; // size is stored in the first two byte, MSB First
			// send up
			if (send_up((char *) outBuf[i] + 2, size)) {
				delete outBuf[i];
				outBuf[i] = 0x00;
				// erial.println("send done");
				return true;
			} else {
				logger.println(TOPIC_CON_REL, F("Send up failed"), COLOR_RED);
				return false;
			}
		}
	}
	return true;
}

// start ap, set IP to "same as main connection +1" master 192.168.N.1 -> AP 192.168.(N+1).1
// create and start wifiserver on port 1883
void connection_relay::startAP(){
	if (!m_AP_running) {
		logger.println(TOPIC_CON_REL, F("Starting AP"));
		WiFi.mode(WIFI_AP_STA);
		IPAddress apSM = IPAddress(255, 255, 255, 0);
		IPAddress ip   = WiFi.localIP();
		ip[2] = ip[2] + 1; // increase 3rd octed for each sublevel
		ip[3] = 1;
		sprintf(m_msg_buffer, "Set AP IP to %i.%i.%i.%i", ip[0], ip[1], ip[2], ip[3]);
		logger.println(TOPIC_CON_REL, m_msg_buffer, COLOR_YELLOW);
		WiFi.softAPConfig(ip, ip, apSM);
		WiFi.softAP(AP_SSID, AP_PW, WiFi.channel(), 0); // 1 == hidden SSID
		m_AP_running = true;

		espServer = new WiFiServer(MESH_PORT);
		espServer->begin();

		logger.println(TOPIC_CON_REL, F("AP started"), COLOR_GREEN);
	}
}

// stop access point and stop server
void connection_relay::stopAP(){
	if (m_AP_running) {
		logger.println(TOPIC_CON_REL, F("Stopping AP"));
		if (espServer) {
			espServer->stop();
			delete espServer;
			espServer = NULL;
		}
		WiFi.softAPdisconnect(true);
		WiFi.mode(WIFI_STA);
		m_AP_running = false;
		logger.println(TOPIC_CON_REL, F("AP stopped"));
	}
}

// this will be called from the main loop
// 1. if we are a mesh node, check if our uplink has info for us. parse it and run callback, which will consume the data or call publish
// 2. check if our server has new incoming connections and handle them
// 3. check all connected clients if new data are available and call the parser if needed
// 4. add keep alives to queue if needed
// 5. send data upstream if there are any in the queue
// 6. if we are directly connected to the MQTT, loop our mqtt client
// 7. disconnect dead mesh clients
void connection_relay::receive_loop(){
	if (m_connection_type == CONNECTION_MESH_CONNECTED) {
		// handle data from uplink (function 1)
		while (espUplink.available()) {
			uint8_t buf;
			espUplink.readBytes(&buf, 1);
			sprintf(m_msg_buffer, "Received data from Uplink");
			logger.println(TOPIC_CON_REL, m_msg_buffer, COLOR_YELLOW);
			if (buf == MSG_TYPE_PUBLISH) {
				uint8_t topic_len;
				uint8_t msg_len;
				espUplink.readBytes(&topic_len, 1);
				espUplink.readBytes(&msg_len, 1);
				char topic[topic_len + 1];
				char msg[msg_len + 1];
				espUplink.readBytes(topic, topic_len);
				espUplink.readBytes(msg, msg_len);
				topic[topic_len] = '\0';
				msg[msg_len]     = '\0';

				callback(topic, (byte *) msg, msg_len);
			}
		}
		// handle keepalives (function 4)
		if ((millis() - espLastcomm) / 1000 > COMM_TIMEOUT / 2) { // 70 sec
			char buf[2];
			buf[0] = MSG_TYPE_PING;
			buf[1] = 0x00;
			enqueue_up(buf, 1);
		}
		// send messages upstream (function 5)
		dequeue_up();
	} else if (m_connection_type == CONNECTION_DIRECT_CONNECTED) {
		// handle data from MQTT link if any (function 6)
		client.loop();
	}
	// new client (function 2)
	if (espServer != NULL) {
		if (espServer->hasClient()) {
			WiFiClient * c = new WiFiClient(espServer->available());
			onClient(c);
		}
	}
	// incoming traffic (function 3) and handle disconnects (function 7)
	for (int i = 0; i < ESP8266_NUM_CLIENTS; i++) {
		if (espClients[i] != NULL) {
			if (espClients[i]->connected()) {
				if (espClients[i]->available()) {
					espClientsLastComm[i] = millis();
					onData(espClients[i]);
				} else if (espClientsLastComm[i] > 0 && (millis() - espClientsLastComm[i]) / 1000 > COMM_TIMEOUT * 1.2) { // 1.2 to be able to miss one and have 20% headroom
					logger.println(TOPIC_CON_REL, F("Client timed out. Disconnecting"), COLOR_YELLOW);
					espClients[i]->stop();
					delete espClients[i];
					espClients[i] = NULL;
				}
			} else {
				logger.println(TOPIC_CON_REL, F("Client disconnected"), COLOR_YELLOW);
				delete espClients[i];
				espClients[i] = NULL;
			}
		}
	}
} // receive_loop

// will be callen by the receive_loop if the server has new espClients
// purpose is to link the client to the pointer structure
void connection_relay::onClient(WiFiClient * c){
	sprintf(m_msg_buffer, "(%s) Incoming client connection", c->remoteIP().toString().c_str());
	logger.println(TOPIC_CON_REL, m_msg_buffer, COLOR_PURPLE);
	for (int i = 0; i < ESP8266_NUM_CLIENTS; i++) {
		if (espClients[i] == NULL) {
			sprintf(m_msg_buffer, "Assign id: %i", i);
			logger.println(TOPIC_CON_REL, m_msg_buffer);

			espClients[i] = c;
			char buf[2];
			buf[0] = MSG_TYPE_ACK;
			buf[1] = 0x00;
			c->write(buf, 1);

			espClientsLastComm[i] = millis();
			break;
		}
	}
}

// will be callen by the receive_loop if the client has data available
// should do the parsing and enqueue_up the response
void connection_relay::onData(WiFiClient * c){
	size_t len = c->available();
	uint8_t data[len];

	c->read(data, len);
	// sprintf(m_msg_buffer,"Got data from %s",c->remoteIP().toString().c_str());
	// logger.println(TOPIC_CON_REL, m_msg_buffer,COLOR_YELLOW);
	char msg_ack[2];
	msg_ack[0] = MSG_TYPE_ACK;
	msg_ack[1] = '\0';
	c->write(msg_ack, 1);
	// erial.println(msg);
	if (((char *) data)[0] == MSG_TYPE_SUBSCRPTION) {
		// [0] type
		// [1..] topic
		uint8_t * topic_start = ((uint8_t *) data) + 1;
		sprintf(m_msg_buffer, "(%s) FWD subscription '%s'", c->remoteIP().toString().c_str(), topic_start);
		logger.println(TOPIC_CON_REL, m_msg_buffer, COLOR_YELLOW);

		subscribe((char *) topic_start, true); // subscribe via enqueue
	} else if (((char *) data)[0] == MSG_TYPE_PUBLISH) {
		// [0] type
		// [1] length of topic
		// [2] length of msg
		uint8_t * topic_start = ((uint8_t *) data) + 3;
		uint8_t * msg_start   = ((uint8_t *) data) + ((uint8_t *) data)[1] + 3;
		sprintf(m_msg_buffer, "(%s) FWD '%s' -> '%s'", c->remoteIP().toString().c_str(), topic_start, msg_start);
		logger.println(TOPIC_CON_REL, m_msg_buffer, COLOR_YELLOW);

		publish((char *) topic_start, (char *) msg_start, true); // publish via enqueue
	} else if (((char *) data)[0] == MSG_TYPE_NW_LOOP_CHECK) {
		if (m_connection_type == CONNECTION_MESH_CONNECTED) { //  don't care it if we're then head, no loop here
			// copy message to send it up the chain
			char msg[len + 1];
			memcpy(msg, (char *) data, len);
			msg[len] = '\0';

			// prepare our own message to compare both
			char msg_check[20]; // 1 + 2*6 + 5
			uint8_t mac[6];
			WiFi.macAddress(mac);
			sprintf(msg_check, "%c%02x:%02x:%02x:%02x:%02x:%02x", MSG_TYPE_NW_LOOP_CHECK, mac[5], mac[4], mac[3], mac[2], mac[1],
			  mac[0]);

			sprintf(m_msg_buffer, "(%s) NETLOOP check '%s' vs '%s'", c->remoteIP().toString().c_str(), msg + 1, msg_check + 1);
			logger.println(TOPIC_CON_REL, m_msg_buffer, COLOR_PURPLE);

			// do the check
			if (strcmp(msg, msg_check) == 0) {
				// fuck .. loop .. disconnect network
				sprintf(m_msg_buffer, "Received own MAC, loop detected. Disconnecting.");
				logger.println(TOPIC_CON_REL, m_msg_buffer, COLOR_RED);
				disconnectServer();
			} else {
				sprintf(m_msg_buffer, "Not my MAC, send on it.");
				logger.println(TOPIC_CON_REL, m_msg_buffer, COLOR_GREEN);
				enqueue_up(msg, len + 1);
			}
		} // no else needed, if we're the direct connected node then this showsn that there is no loop
	} else if (((char *) data)[0] == MSG_TYPE_ROUTING) {
		// preparation
		if (strlen(mqtt.dev_short) > 6) { // to make sure
			mqtt.dev_short[6] = 0x00;
		}
		// [0] type
		// [1] length of topic
		// [2] length of msg
		// [3..] topic, which will end with 0x00
		// [..] message, which will end with 0x00
		char msg[len + 5 + strlen(mqtt.dev_short)];                             // what ever is in it + my name + " -> '\0'"
		memcpy(msg, data, len);                                                 // copy existing message, this includes th \0
		memcpy(msg + len - 1, " >> ", 4);                                       // -1 to replace \0
		memcpy(msg + len - 1 + 4, &mqtt.dev_short, strlen(mqtt.dev_short) + 1); // copy name + \0

		uint8_t * topic_start = ((uint8_t *) msg) + 3;
		uint8_t * msg_start   = ((uint8_t *) msg) + ((uint8_t *) msg)[1] + 3;
		sprintf(m_msg_buffer, "(%s) FWD routing '%s' -> '%s'", c->remoteIP().toString().c_str(), topic_start, msg_start);
		logger.println(TOPIC_CON_REL, m_msg_buffer, COLOR_YELLOW);

		if (m_connection_type == CONNECTION_DIRECT_CONNECTED) {
			publish((char *) topic_start, (char *) msg_start);
		} else {
			enqueue_up(msg, len + 4 + strlen(mqtt.dev_short)); // len-1+4+strlen()+1
		}
	} else if (((char *) data)[0] == MSG_TYPE_PING) {
		sprintf(m_msg_buffer, "Received keep alive from '%s'", c->remoteIP().toString().c_str());
		logger.println(TOPIC_CON_REL, m_msg_buffer, COLOR_GREEN);
	} else {
		sprintf(m_msg_buffer, "Received unsupported message type '%i'", ((char *) data)[0]);
		logger.println(TOPIC_CON_REL, m_msg_buffer, COLOR_RED);
	}
} // onData
