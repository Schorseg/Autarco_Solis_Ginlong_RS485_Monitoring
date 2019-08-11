import appdaemon.plugins.hass.hassapi as hass

class PresentsWorld(hass.Hass):

	def initialize(self):
		self.log("Starting Presents Service")
		self.listen_state(self.presents, "binary_sensor.someone_is_home", duration = 1*60) # 1 min away


	def presents(self, entity, attribute, old, new,kwargs):
		if(self.get_state("binary_sensor.someone_is_home") == "off"):
			self.log("Presents state just changed to 'nobody is home'")
			# get all device that are on
			remaining_lights_on = []
			group = self.get_state("group.all_lights", attribute = "all")
			for l in group["attributes"]["entity_id"]:
				if(self.get_state(l)=="on"):
					remaining_lights_on.append(l)
			# if there are some, write a message
			if(len(remaining_lights_on)>0):
				m = "Hi, it seams like nobody is home now for over a minute but there is still light: "
				for l in remaining_lights_on:
					m += self.friendly_name(l)+" ("+l+"), "
				m = m[0:len(m)-2]+" so you might want to tunnel in and turn it off remotly" # remove tailing ", "
				self.log(m)
				self.call_service("notify/pb", title="connection alert", message=m)