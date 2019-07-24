#ifndef energy_meter_h
	#define energy_meter_h


#include "main.h"
#include "../lib/softserial/SoftwareSerial.h"

	static constexpr char MQTT_ENERGY_METER_TOTAL_TOPIC[]    = "em_tot"; // publish
	static constexpr char MQTT_ENERGY_METER_CUR_TOPIC[] = "em_cur"; // publish

	static constexpr char ENERGY_METER_TOTAL[]    = "1-0:1.8.0*255("; // publish
	static constexpr char ENERGY_METER_CUR[]    = "1-0:16.7.0*255("; // publish

#ifdef WITH_DISCOVERY
	static constexpr char MQTT_DISCOVERY_EM_CUR_TOPIC[]      = "homeassistant/sensor/%s_em_cur/config";
	static constexpr char MQTT_DISCOVERY_EM_CUR_MSG[]      = "{\"name\":\"%s_em_cur\", \"stat_t\": \"%s/r/em_cur\"}";
	static constexpr char MQTT_DISCOVERY_EM_TOTAL_TOPIC[]      = "homeassistant/sensor/%s_em_tot/config";
	static constexpr char MQTT_DISCOVERY_EM_TOTAL_MSG[]      = "{\"name\":\"%s_em_tot\", \"stat_t\": \"%s/r/em_tot\"}";
#endif

	#define ENEGERY_METER_TOTAL 0
	#define ENEGERY_METER_CUR 1


	class energy_meter : public capability {
public:
		energy_meter();
		~energy_meter();
		bool init();
		bool loop();
		bool intervall_update(uint8_t slot);
		bool subscribe();
		uint8_t count_intervall_update();
		bool parse(uint8_t * config);
		uint8_t * get_key();
		bool publish();
private:
		mqtt_parameter_16 m_state;
		char m_current_w[10]; // 6 vor, 2 nachkomma
		char m_total_kwh[16]; // 6 vor, 8 nachkomma
		char temp[16];
		SoftwareSerial * swSer1;
		uint8_t dataset;
		char* identifier[2];
		bool m_discovery_pub;
	};


#endif // ifndef energy_meter_h
