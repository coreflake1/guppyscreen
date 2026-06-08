import logging

TRINAMIC_DRIVERS = ["tmc2130", "tmc2208", "tmc2209", "tmc2240", "tmc2660", "tmc5160"]

class TMCStatus:
    def __init__(self, config):
        self.config = config
        self.printer = config.get_printer()
        self.configured_steppers = []
        self.sense_resistor = {}
        self.tmcs = {}

        for driver in TRINAMIC_DRIVERS:
            for n in self.config.get_prefix_sections(driver):
                name = n.get_name()
                self.configured_steppers.append(name)
                # Read the real configured sense resistor per driver (was a
                # hardcoded 2209-only table before, which broke i_rms on 2208).
                self.sense_resistor[name] = n.getfloat('sense_resistor', None)

        self.handle_connect()

    def handle_connect(self):
        for s in self.configured_steppers:
            self.tmcs[s] = self.printer.lookup_object(s)

    def get_status(self, eventtime):
        data = {}
        for tmc, tmcobj in self.tmcs.items():
            # Never let one driver's read shut down Klipper. Skip on error.
            try:
                data[tmc] = self._collect(tmc, tmcobj)
            except Exception as e:
                logging.warning("tmcstatus: skipping %s: %s", tmc, e)
        return data

    def _collect(self, tmc, tmcobj):
        fobj = tmcobj.fields

        # Safe field read: None if the driver lacks the field (e.g. the
        # CoolStep/StallGuard fields that don't exist on a TMC2208).
        def gf(field):
            if fobj.lookup_register(field, None) is not None:
                return fobj.get_field(field)
            return None

        drv_status_val = tmcobj.mcu_tmc.get_register('DRV_STATUS')
        fields = fobj.get_reg_fields('DRV_STATUS', drv_status_val)
        drv_fields = {n: v for n, v in fields.items() if v}
        tmc_data = {
            'drv_status': drv_fields,

            'hstrt': gf('hstrt'),
            'hend': gf('hend'),

            'pwm_autoscale': gf('pwm_autoscale'),
            'pwm_autograd': gf('pwm_autograd'),
            'pwm_grad': gf('pwm_grad'),
            'pwm_ofs': gf('pwm_ofs'),
            'pwm_reg': gf('pwm_reg'),
            'pwm_lim': gf('pwm_lim'),
            'tpwmthrs': gf('tpwmthrs'),

            'en_spreadcycle': gf('en_spreadcycle'),
            'tbl': gf('tbl'),
            'toff': gf('toff'),

            # CoolStep/StallGuard — absent on TMC2208, guarded -> None
            'tcoolthrs': gf('tcoolthrs'),
            'semin': gf('semin'),
            'semax': gf('semax'),
            'seup': gf('seup'),
            'sedn': gf('sedn'),
            'seimin': gf('seimin'),
        }

        # SG_RESULT register only on StallGuard-capable drivers (2209+)
        if fobj.lookup_register('sg_result', None) is not None:
            tmc_data['sg_result'] = tmcobj.mcu_tmc.get_register('SG_RESULT')

        if 'cs_actual' in drv_fields:
            irms = self._cs_to_rms(drv_fields['cs_actual'], tmc, tmcobj)
            if irms is not None:
                tmc_data['i_rms'] = irms

        if fobj.lookup_register('en_pwm_mode', None) is not None:
            tmc_data['en_pwm_mode'] = fobj.get_field('en_pwm_mode')

        return tmc_data

    def _cs_to_rms(self, cs, tmc, tmcobj):
        rsense = self.sense_resistor.get(tmc)
        if rsense is None:
            return None
        vsense = tmcobj.fields.get_field('vsense')
        return (cs+1)/32.0 * (0.180 if vsense == 1 else 0.325)/(rsense+0.02) / 1.41421 * 1000

def load_config(config):
    return TMCStatus(config)
