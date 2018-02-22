/*
if connected to "mqtt server" start AP and start IP Server 192.168.10.1
on incoming message from IP server -> send via mqtt route
on incoming IP connection, "handle connection"
on incoming IP request, check aount of connected clients and return next free
on incoming message from MQTT Server, if not for us -> send to all clients
if connect to wifi fails -> run wifi Scan
if wifi scan shows hidden AP with ESP_MESH as SSID try to connect with ip 192.168.10.254
on conncect to mesh send IP message "get next free ip"
on Next IP response, reassign IP

*/
#include "connection_relay.h"

connection_relay::connection_relay(){
	espServer = new AsyncServer(MESH_PORT);
	espServer->setNoDelay(true);
	m_AP_running = false;
	m_connection_type = CONNECTION_DIRECT_CONNECTED; // wifi STA connection can survive reboots
};
connection_relay::~connection_relay(){};

void connection_relay::begin(){}

int8_t connection_relay::scan(bool blocking){
	int8_t status;
	if(blocking){
		// when should we run WiFi.scanDelete();?
		status = WiFi.scanNetworks();
		return status;
	} else {
		status = WiFi.scanComplete();
		if(status == -2){ // scan not triggered
			  WiFi.scanNetworks(true);
				return 0; // not complete
		} else if(status == -1){ // scan still ongoing
			return 0; // not complete
		}
		return status; // complete
	}
	return 0;
}

bool connection_relay::DirectConnect(){
	logger.println(TOPIC_WIFI, F("Currently not connected, initiate direct connection ..."), COLOR_RED);
	logger.println(TOPIC_WIFI, F("Removing  all old credentials ..."), COLOR_YELLOW);
	// mqtt.nw_ssid, mqtt.nw_pw or autoconnect?
	WiFi.disconnect();
	logger.print(TOPIC_WIFI, F("Connecting to: "), COLOR_YELLOW);
	logger.pln(mqtt.nw_ssid);

	if(wifiManager.connectWifi(mqtt.nw_ssid, mqtt.nw_pw)==WL_CONNECTED){
		m_connection_type = CONNECTION_DIRECT_CONNECTED;
		return true;
	}
	return false;
}

bool connection_relay::MeshConnect(){
	int8_t scan_count = scan(true); // will start with blocking scan
	int8_t best_RSSI=-1;
	if(scan_count>0){
		//Serial.printf("%d network(s) found\r\n", scan_count);
		for (int i = 0; i < scan_count; i++)
		{
			//Serial.printf("%d: %s, Ch:%d (%ddBm) %s %i\n\r", i+1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i) == ENC_TYPE_NONE ? "open" : "", WiFi.isHidden(i));
			//if(WiFi.isHidden(i) && strncmp(WiFi.SSID(i).c_str(),AP_SSID,strlen(AP_SSID))==0){
			if(strncmp(WiFi.SSID(i).c_str(),AP_SSID,strlen(AP_SSID))==0){
				//Serial.println("interesting config");
				if(best_RSSI==-1 || WiFi.RSSI(i) > WiFi.RSSI(best_RSSI)){
					//Serial.println("actually the best");
					best_RSSI=i;
				}
			}
		}

		if(best_RSSI>-1){
			sprintf(m_msg_buffer,"%i Networks found, incl mesh [%idbm]. Connecting",scan_count,WiFi.RSSI(best_RSSI));
			logger.println(TOPIC_WIFI, m_msg_buffer, COLOR_GREEN);

			// connect to AP
			WiFi.persistent(false);    //avoids updating the same flash cell every reboot
			WiFi.begin(WiFi.SSID(best_RSSI).c_str(), AP_PW, 0, WiFi.BSSID(best_RSSI), true);
			WiFi.waitForConnectResult();
			if(WiFi.status() == WL_CONNECTED){
				sprintf(m_msg_buffer,"Mesh connection established %i.%i.%i.%i",WiFi.localIP()[0],WiFi.localIP()[1],WiFi.localIP()[2],WiFi.localIP()[3]);
				logger.println(TOPIC_WIFI, m_msg_buffer, COLOR_GREEN);
				m_connection_type = CONNECTION_MESH_CONNECTED;
				return true;
			}
		} else {
			sprintf(m_msg_buffer,"No mesh in range");
			logger.print(TOPIC_WIFI, m_msg_buffer, COLOR_RED);
		}
	}
	return false;
}

bool connection_relay::connectServer(char* dev_short, char* login, char* pw){
	if(m_connection_type == CONNECTION_DIRECT_CONNECTED){
		sprintf(m_msg_buffer,"Establishing direct MQTT link");
		logger.println(TOPIC_MQTT, m_msg_buffer, COLOR_YELLOW);

		client.setServer(mqtt.server_ip, atoi(mqtt.server_port));
		client.setCallback(callback); // in main.cpp
		return client.connect(dev_short, login, pw);
	} else if(m_connection_type == CONNECTION_MESH_CONNECTED){

		sprintf(m_msg_buffer,"Establishing indirect MESH-MQTT link");
		logger.println(TOPIC_MQTT, m_msg_buffer, COLOR_YELLOW);

		IPAddress apIP = WiFi.localIP();
		apIP[3]=1;
		return espUplink.connect(apIP,MESH_PORT);
	}
	return false;
}

// suppose to send a raw message around in the network
bool connection_relay::loopCheck(){
	if(m_connection_type == CONNECTION_MESH_CONNECTED){
		sprintf(m_msg_buffer,"sending NETLOOP check up the chain");
		logger.println(TOPIC_CON_REL, m_msg_buffer,COLOR_PURPLE);

		char msg[25];
		uint8_t mac[6];
		WiFi.macAddress(mac);
		sprintf(msg, "%c%02x:%02x:%02x:%02x:%02x:%02x",MSG_TYPE_NW_LOOP_CHECK, mac[5], mac[4], mac[3], mac[2],	mac[1],	mac[0]);
		send_up(msg);
	}
}

void connection_relay::disconnectServer(){
	if(m_connection_type == CONNECTION_DIRECT_CONNECTED){
		client.disconnect();
	} else if(m_connection_type == CONNECTION_MESH_CONNECTED){
		espUplink.stop();
	}
}

bool connection_relay::connected(){
	if(m_connection_type == CONNECTION_DIRECT_CONNECTED){
		return client.connected();
	} else if(m_connection_type == CONNECTION_MESH_CONNECTED){
		return espUplink.connected();
	}
	return false;
}

bool connection_relay::subscribe(char* topic){
	return subscribe(topic,false);
}

bool connection_relay::subscribe(char* topic, bool enqueue){
	if(!connected()){
		return false;
	}
	if(m_connection_type == CONNECTION_DIRECT_CONNECTED){
		//Serial.print("Direct subscibe: ");
		//Serial.println(topic);
		client.subscribe(topic); // MQTT_TRACE_TOPIC topic
		client.loop();
		return client.connected();
	} else if(m_connection_type == CONNECTION_MESH_CONNECTED){
		char msg[strlen(topic)+2];
		msg[0]=MSG_TYPE_SUBSCRPTION;
		memcpy(msg+1,topic,strlen(topic));
		msg[strlen(topic)+1]='\0';
		// send or enqueue
		if(enqueue){
			return enqueue_up(msg);
		} else {
			return send_up(msg);
		}
	}
	return false;
}

bool connection_relay::publish(char* topic, char* mqtt_msg){
	return publish(topic,mqtt_msg,false);
}
bool connection_relay::publish(char* topic, char* mqtt_msg,bool enqueue){
	if(!connected()){
		return false;
	}
	if(m_connection_type == CONNECTION_DIRECT_CONNECTED){
		client.publish(topic, mqtt_msg, true);
		client.loop();
		return client.connected();
	} else if(m_connection_type == CONNECTION_MESH_CONNECTED){
		char msg[strlen(topic)+strlen(mqtt_msg)+4];
		msg[0]=MSG_TYPE_PUBLISH;
		msg[1]=strlen(topic);
		msg[2]=strlen(mqtt_msg);
		memcpy(msg+3,topic,strlen(topic));
		memcpy(msg+3+strlen(topic),mqtt_msg,strlen(mqtt_msg));
		msg[strlen(topic)+strlen(mqtt_msg)+4-1]='\0';
		// send or enqueue
		if(enqueue){
			return enqueue_up(msg);
		} else {
			return send_up(msg);
		}
	}
	return false;
}

// used to send messages fro the mqtt server downstream to all connected clients
bool connection_relay::broadcast_publish_down(char* topic, char* mqtt_msg){
	sprintf(m_msg_buffer,"(down) FWD '%s' -> '%s'",topic,mqtt_msg);
	logger.println(TOPIC_CON_REL, m_msg_buffer, COLOR_YELLOW);

	char msg[strlen(topic)+strlen(mqtt_msg)+4];
	msg[0]=MSG_TYPE_PUBLISH;
	msg[1]=strlen(topic);
	msg[2]=strlen(mqtt_msg);
	memcpy(msg+3,topic,strlen(topic));
	memcpy(msg+3+strlen(topic),mqtt_msg,strlen(mqtt_msg));
	msg[strlen(topic)+strlen(mqtt_msg)+4-1]='\0';
	for (int i = 0; i <= ESP8266_NUM_CLIENTS; i++) {
		if (espClients[i]) {
			espClients[i]->write(msg);
		}
	}
	return true;
}

// send pre-encoded messages (subscriptions/pubish's) UP to our mesh server
bool connection_relay::send_up(char* msg){
	if(!connected()){
		return false;
	}
	uint32_t start = millis();
	uint8_t buf;
	//send up
	espUplink.print(msg);
	// waitup to 5 sec for ACK
	while((millis()-start<5000) || espUplink.available()){
		Serial.println(millis()-start);
		delay(100);
		if(espUplink.available()){
			Serial.printf("-2..%i\r\n",ESP.getFreeHeap());
			delay(100);
			espUplink.read(&buf,1);
			//Serial.printf("recv %i %c\r\n",buf,buf);
			if(buf==MSG_TYPE_ACK){
				Serial.println("-3..");
				delay(100);
				return true;
			}
		}
	}
	return false;
}

bool connection_relay::enqueue_up(char* msg){
	uint8_t* buf;
	buf = new uint8_t[strlen(msg)];
	if(buf){
		memcpy(buf,msg,strlen(msg));
		for(uint8_t i=0; i<MAX_MSG_QUEUE; i++){
			if(!outBuf[i]){
				Serial.print("Enqueue Slot ");
				Serial.println(i);

				outBuf[i]=buf;
				return true;
			}
		}
	}
	return false;
}

bool connection_relay::dequeue_up(){
	uint32_t start;
	uint8_t buf;
	for(uint8_t i=0; i<MAX_MSG_QUEUE; i++){
		if(outBuf[i]){
			start = millis();
			Serial.print("Dequeue Slot ");
			Serial.println(i);
			//send up
			if(send_up((char*)outBuf[i])){
				delete outBuf[i];
				outBuf[i]=0x00;
			}
		}
	}
	return true;
}

void connection_relay::startAP(){
	if(!m_AP_running){
		logger.println(TOPIC_CON_REL, F("Starting AP"));
		WiFi.mode(WIFI_AP_STA);
		IPAddress apSM = IPAddress(255, 255, 255, 0);
		IPAddress ip = WiFi.localIP();
		ip[2]=ip[2]+1; // increase 3rd octed for each sublevel
		ip[3]=1;
		sprintf(m_msg_buffer,"Set AP IP to %i.%i.%i.%i",ip[0],ip[1],ip[2],ip[3]);
		logger.println(TOPIC_CON_REL, m_msg_buffer,COLOR_YELLOW);
		WiFi.softAPConfig(ip, ip, apSM);
		WiFi.softAP(AP_SSID, AP_PW, WiFi.channel(), 0); // 1 == hidden SSID
		m_AP_running = true;

		espServer->onClient( [this](void * arg, AsyncClient *c){ this->onClient(c);  }, this);
		espServer->begin();

		logger.println(TOPIC_CON_REL, F("AP started"),COLOR_GREEN);
	}
}


void connection_relay::stopAP(){
	if(m_AP_running){
		logger.println(TOPIC_CON_REL, F("Stopping AP"));
		WiFi.softAPdisconnect(true);
		WiFi.mode(WIFI_STA);
		m_AP_running = false;
		logger.println(TOPIC_CON_REL, F("AP stopped"));
	}
}

void connection_relay::receive_loop(){
	if(m_connection_type == CONNECTION_MESH_CONNECTED){
		while(espUplink.available()){
			//uint8_t len = espUplink.available();
			uint8_t buf;
			espUplink.readBytes(&buf,1);
			if(buf == MSG_TYPE_PUBLISH){
				uint8_t topic_len;
				uint8_t msg_len;
				espUplink.readBytes(&topic_len,1);
				espUplink.readBytes(&msg_len,1);
				char topic[topic_len+1];
				char msg[msg_len+1];
				espUplink.readBytes(topic,topic_len);
				espUplink.readBytes(msg,msg_len);
				topic[topic_len]='\0';
				msg[msg_len]='\0';

				callback(topic,(byte*)msg,msg_len);
			}
		}
	} else if(m_connection_type == CONNECTION_DIRECT_CONNECTED){
		client.loop();
	}
	dequeue_up();
}

void connection_relay::onClient(AsyncClient* c) {
	sprintf(m_msg_buffer,"(%s) Incoming client connection",c->remoteIP().toString().c_str());
	logger.println(TOPIC_CON_REL, m_msg_buffer, COLOR_PURPLE);
	for (int i = 3; i <= ESP8266_NUM_CLIENTS; i++) {
		if (!espClients[i]) {
			sprintf(m_msg_buffer,"Assign id: %i",i);
			logger.println(TOPIC_CON_REL, m_msg_buffer);

			espClients[i] = c;
			espClients[i]->onDisconnect([this](void * arg, AsyncClient *c)                           { this->onDisconnect(c);      }, this);
			espClients[i]->onError(     [this](void * arg, AsyncClient *c, int8_t error)             { this->onError(c, error);    }, this);
			//espClients[i]->onAck(       [this](void * arg, AsyncClient *c, size_t len, uint32_t time){ this->onAck(c, len, time);  }, this);
			espClients[i]->onTimeout(   [this](void * arg, AsyncClient *c, uint32_t time)            { this->onTimeout(c, time);   }, this);
			espClients[i]->onData(      [this](void * arg, AsyncClient *c, void* data, size_t len)   { this->onData(c, data, len); }, this);
			break;
		}
	}
}

void connection_relay::onDisconnect(AsyncClient* c) {
	for (int i = 0; i <= ESP8266_NUM_CLIENTS; i++) {
	    if (c == espClients[i]) {
	        logger.println(TOPIC_CON_REL, F("Client disconnected"),COLOR_YELLOW);
	        delete espClients[i];
	        espClients[i] = NULL;
					return;
	    }
	}
	logger.println(TOPIC_CON_REL, F("Disconnected unknown client"),COLOR_YELLOW);
}


void connection_relay::onError(AsyncClient* c, int8_t error) {
	sprintf(m_msg_buffer,"Got error on %s : %s",c->remoteIP().toString().c_str(),String(error).c_str());
	logger.println(TOPIC_CON_REL, m_msg_buffer,COLOR_YELLOW);
	c->close();
}

void connection_relay::onTimeout(AsyncClient* c, uint32_t time) {
  //logger.println(TOPIC_CON_REL, "Got timeout  " + c->remoteIP().toString() + ": " + String(time),COLOR_YELLOW);
  c->close();
}

void connection_relay::onData(AsyncClient* c, void* data, size_t len) {
	//sprintf(m_msg_buffer,"Got data from %s",c->remoteIP().toString().c_str());
	//logger.println(TOPIC_CON_REL, m_msg_buffer,COLOR_YELLOW);
	char msg_ack[2];
	msg_ack[0]=MSG_TYPE_ACK;
	msg_ack[1]='\0';
	c->write(msg_ack,1);
	//Serial.println(msg);
	if(((char*)data)[0] == MSG_TYPE_SUBSCRPTION){
		char msg[len+1];
		memcpy(msg,(char*)data+1,len-1);
		msg[len-1]='\0';

		sprintf(m_msg_buffer,"(%s) FWD subscription '%s'", c->remoteIP().toString().c_str(), msg);
		logger.println(TOPIC_CON_REL, m_msg_buffer,COLOR_YELLOW);

		subscribe(msg,true); //subscribe via enqueue
	} else if(((char*)data)[0] == MSG_TYPE_PUBLISH){
		char topic[((uint8_t*)data)[1]+1];
		char msg[((uint8_t*)data)[2]+1];
		memcpy(topic,(char*)data+3,((uint8_t*)data)[1]);
		topic[((uint8_t*)data)[1]]='\0';
		memcpy(msg,(char*)data+3+((uint8_t*)data)[1],((uint8_t*)data)[2]);
		msg[((uint8_t*)data)[2]]='\0';
		sprintf(m_msg_buffer,"(%s) FWD '%s' -> '%s'", c->remoteIP().toString().c_str(),topic,msg);
		logger.println(TOPIC_CON_REL, m_msg_buffer,COLOR_YELLOW);

		publish(topic,msg,true); // publish via enqueue
	} else if(((char*)data)[0] == MSG_TYPE_NW_LOOP_CHECK){
		if(m_connection_type == CONNECTION_MESH_CONNECTED){ //  don't care it if we're then head, no loop here
			// copy message to send it up the chain
			char msg[len+1];
			memcpy(msg,(char*)data,len);
			msg[len]='\0';

			// prepare our own message to compare both
			char msg_check[20]; // 1 + 2*6 + 5
			uint8_t mac[6];
			WiFi.macAddress(mac);
			sprintf(msg_check, "%c%02x:%02x:%02x:%02x:%02x:%02x",MSG_TYPE_NW_LOOP_CHECK, mac[5], mac[4], mac[3], mac[2],	mac[1],	mac[0]);

			sprintf(m_msg_buffer,"(%s) NETLOOP check '%s' vs '%s'", c->remoteIP().toString().c_str(), msg+1, msg_check+1);
			logger.println(TOPIC_CON_REL, m_msg_buffer,COLOR_PURPLE);

			// do the check
			if(strcmp(msg,msg_check)==0){
				// fuck .. loop .. disconnect network
				disconnectServer();
			} else {
				enqueue_up(msg);
			}
		}
	} else {
		sprintf(m_msg_buffer,"Received unsupported message type '%i'", ((char*)data)[0]);
		logger.println(TOPIC_CON_REL, m_msg_buffer,COLOR_RED);
	}
}
