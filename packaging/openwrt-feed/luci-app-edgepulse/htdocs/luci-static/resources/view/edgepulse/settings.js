'use strict';
'require view';
'require form';

return view.extend({
	render: function() {
		var m = new form.Map('edgepulse', _('EdgePulse Settings'));
		var s = m.section(form.NamedSection, 'main', 'edgepulse');
		var o;

		o = s.option(form.Flag, 'enabled', _('Enabled'));
		o.default = o.enabled;

		o = s.option(form.Value, 'sample_interval_sec', _('Sample interval'));
		o.datatype = 'uinteger';
		o.default = '5';

		o = s.option(form.Value, 'feature_interval_sec', _('Feature interval'));
		o.datatype = 'uinteger';
		o.default = '60';

		o = s.option(form.Value, 'retention_raw_sec', _('Raw retention'));
		o.datatype = 'uinteger';
		o.default = '3600';

		o = s.option(form.Value, 'retention_feature_sec', _('Feature retention'));
		o.datatype = 'uinteger';
		o.default = '86400';

		o = s.option(form.Value, 'db_path', _('Database path'));
		o.default = '/tmp/edgepulse/edgepulse.db';

		s.option(form.Flag, 'enable_cpu', _('CPU collector'));
		s.option(form.Flag, 'enable_memory', _('Memory collector'));
		s.option(form.Flag, 'enable_network', _('Network collector'));
		s.option(form.Flag, 'enable_thermal', _('Thermal collector'));
		s.option(form.Flag, 'enable_wireless', _('Wireless collector'));
		s.option(form.Flag, 'enable_conntrack', _('Conntrack collector'));

		return m.render();
	}
});
