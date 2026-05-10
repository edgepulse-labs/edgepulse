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
		s.option(form.Flag, 'enable_nft', _('nftables collector'));

		s = m.section(form.NamedSection, 'agent', 'agent', _('AI Agent'));

		o = s.option(form.Flag, 'enabled', _('Enabled'));
		o.default = o.disabled;

		o = s.option(form.Flag, 'local_only', _('Local only'));
		o.default = o.enabled;

		o = s.option(form.Flag, 'memory_enabled', _('Memory'));
		o.default = o.enabled;

		o = s.option(form.Flag, 'shell_enabled', _('Shell tools'));
		o.default = o.enabled;

		o = s.option(form.Flag, 'ubus_enabled', _('ubus tools'));
		o.default = o.enabled;

		o = s.option(form.ListValue, 'policy_profile', _('Policy'));
		o.value('read_only', _('Read only'));
		o.default = 'read_only';

		o = s.option(form.Value, 'request_timeout_sec', _('Request timeout'));
		o.datatype = 'uinteger';
		o.default = '60';

		o = s.option(form.Value, 'heartbeat_interval_sec', _('Heartbeat interval'));
		o.datatype = 'uinteger';
		o.default = '60';

		o = s.option(form.Value, 'tool_timeout_sec', _('Tool timeout'));
		o.datatype = 'uinteger';
		o.default = '5';

		o = s.option(form.Value, 'max_tool_output_bytes', _('Max tool output'));
		o.datatype = 'uinteger';
		o.default = '8192';

		s = m.section(form.TypedSection, 'model', _('AI Models'));
		s.anonymous = false;
		s.addremove = true;

		o = s.option(form.Flag, 'enabled', _('Enabled'));
		o.default = o.disabled;

		o = s.option(form.Value, 'role', _('Role'));
		o.default = 'planner,analyzer,responder';

		o = s.option(form.Value, 'base_url', _('Base URL'));
		o.placeholder = 'https://api.example.com/v1';

		o = s.option(form.Value, 'model', _('Model'));

		o = s.option(form.Value, 'api_key', _('API key'));
		o.password = true;

		o = s.option(form.Value, 'api_key_env', _('API key environment'));
		o.default = 'EDGEPULSE_AI_API_KEY';

		o = s.option(form.Value, 'timeout_sec', _('Timeout'));
		o.datatype = 'uinteger';
		o.default = '30';

		o = s.option(form.Value, 'retry_count', _('Retries'));
		o.datatype = 'uinteger';
		o.default = '1';

		return m.render();
	}
});
