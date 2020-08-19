import appdaemon.plugins.hass.hassapi as hass
import datetime, time,random

#
# Hellow World App
#
# Args: GartenWasserWashinge
#

class WashingWorld(hass.Hass):
	def initialize(self):
		self.log("Starting Washing Service")
		self.state_wm = 0
		self.state_light = 0

		self.thr_power = 100
		self.thr_min = 10

		self.wash_ts = datetime.datetime.now().time()
		self.light_ts = datetime.datetime.now().time()
		self.listen_state(self.update_wm, "sensor.dev16_ads_ch0_kw")
		self.listen_state(self.update_light, "sensor.dev16_ads_ch10_kw")

		self.handle_m = []
		self.handle_t = []

	def update_light(self, entity='', attribute='', old='', new='',kwargs=''):
		try:
			if(self.get_state("switch.dev40_gpio_12")=="on"):
				new = float(new)-280
			if(float(new)>125):
				self.state_light += 1
				if(self.state_light==20):
					t="Washing machine"
					m="Hi, downstairs bathroom light on for "+str(self.state_light)+" min"
					self.call_service("notify/pb", title=t, message=m)
					self.call_service("notify/pb_c", title=t, message=m)
			else:
				self.state_light += 0
		except:
			pass

	def update_wm(self, entity='', attribute='', old='', new='',kwargs=''):
		#self.log("updated power to: "+new+" current state is "+str(self.state))
		if(old=="unknown" or new=="unknown"):
		   self.log("unknown!")
		   return
		if(self.state_wm < 20):
			#self.log("state <2")
			if(float(new) >= self.thr_power):
				self.log("high power, inc state_wm")
				self.state_wm += 1
			if(self.state_wm == 20):
				self.log("state_wm == 20, setting time")
				self.wash_ts = datetime.datetime.now()
		elif(self.state_wm == 20):
			if(float(new) >= self.thr_power):
				#self.log("high power, washing")
				self.wash_ts = datetime.datetime.now()
			elif(self.wash_ts + datetime.timedelta(minutes=self.thr_min) < datetime.datetime.now()):
				self.log("low power, for 10 minutes, i guess we're done")
				self.state_wm = 30

				self.handle_m.clear()
				self.handle_m.append(self.listen_state(self.motion, "binary_sensor.dev59_motion_13"))
				self.handle_m.append(self.listen_state(self.motion, "binary_sensor.dev59_motion_16"))
				self.handle_m.append(self.listen_state(self.motion, "binary_sensor.dev54_motion_2"))

				t="Washing machine"
				m="Hi, my work is done :) your beloved washing machine"
				self.call_service("notify/pb", title=t, message=m)
				self.call_service("notify/pb_c", title=t, message=m)
				self.handle_t.clear()
				self.handle_t.append(self.run_in(self.chk,30*60))
				self.handle_t.append(self.run_in(self.chk,60*60))
				self.handle_t.append(self.run_in(self.chk,90*60))
				self.handle_t.append(self.run_in(self.chk,120*60))
			#else:
				#self.log("low power, but during 10 min backoff time")

	def motion(self, entity='', attribute='', old='', new='',kwargs=''):
		self.log("motion: "+entity+" new="+new+" state_wm "+str(self.state_wm))
		if(self.state_wm>=30):
			if(new=="on"):
				try:
					for i in self.handle_t:
						self.cancel_timer(i)
					for a in self.handle_m:
						self.cancel_listen_state(a)
				except:
					pass
				self.state_wm = 0

	def chk(self, entity='', attribute='', old='', new='',kwargs=''):
		self.log("chk, state_wm is "+str(self.state_wm))
		t="Washing machine"
		if(self.state_wm==30): #30
			m=":*"
			self.state_wm = 31
		elif(self.state_wm==31): #60
			m="whenever you're ready"
			self.state_wm = 32
		elif(self.state_wm==32): #90
			m="waiting for you"
			self.state_wm = 33
		elif(self.state_wm==33): #120
			m="honey, I miss you"
			self.state_wm = 0
			for a in self.handle_m:
				self.cancel_listen_state(a)
		self.call_service("notify/pb", title=t, message=m)
		self.call_service("notify/pb_c", title=t, message=m)
