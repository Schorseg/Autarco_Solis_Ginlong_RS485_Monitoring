#ifndef mqttp_h
#define mqttp_h
#include <Arduino.h>

class mqtt_parameter_8 {
	public:
		mqtt_parameter_8();
		void check_set(uint8_t input);
		void set(uint8_t input);
		void set(uint8_t input, bool update);
		uint8_t get_value();
		void outdated();
		void outdated(bool update_required);
		bool get_outdated();
	private:
		uint8_t _value;
		boolean _update_required;

};

class mqtt_parameter_16 {
	public:
		mqtt_parameter_16();
		void check_set(uint16_t input);
		void set(uint16_t input);
		void set(uint16_t input, bool update);
		uint16_t get_value();
		void outdated();
		void outdated(bool update_required);
		bool get_outdated();
	private:
		uint16_t _value;
		boolean _update_required;
};

class mqtt_parameter_s16 {
	public:
		mqtt_parameter_s16();
		void check_set(int16_t input);
		void set(int16_t input);
		void set(int16_t input, bool update);
		int16_t get_value();
		void outdated();
		void outdated(bool update_required);
		bool get_outdated();
	private:
		int16_t _value;
		boolean _update_required;
};

class mqtt_parameter_32 {
	public:
		mqtt_parameter_32();
		void check_set(uint32_t input);
		void set(uint32_t input);
		void set(uint32_t input, bool update);
		uint32_t get_value();
		void outdated();
		void outdated(bool update_required);
		bool get_outdated();
	private:
		uint32_t _value;
		boolean _update_required;
};

class mqtt_parameter_s32 {
	public:
		mqtt_parameter_s32();
		void check_set(int32_t input);
		void set(int32_t input);
		void set(int32_t input, bool update);
		int32_t get_value();
		void outdated();
		void outdated(bool update_required);
		bool get_outdated();
	private:
		int32_t _value;
		boolean _update_required;
};

class mqtt_parameter_64 {
	public:
		mqtt_parameter_64();
		void check_set(uint64_t input);
		void set(uint64_t input);
		void set(uint64_t input, bool update);
		uint64_t get_value();
		void outdated();
		void outdated(bool update_required);
		bool get_outdated();
	private:
		uint64_t _value;
		boolean _update_required;
};

class mqtt_parameter_float {
	public:
		mqtt_parameter_float();
		void check_set(float input);
		void set(float input);
		void set(float input, bool update);
		float get_value();
		void outdated();
		void outdated(bool update_required);
		bool get_outdated();
	private:
		float _value;
		boolean _update_required;
};


#endif
