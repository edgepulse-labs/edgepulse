'use strict';
'require view';
'require fs';
'require ui';

return view.extend({
	load: function() {
		return fs.exec_direct('/usr/libexec/edgepulse-luci', [ 'agent-status' ])
			.catch(function(err) {
				return JSON.stringify({ error: String(err) });
			});
	},

	runDiagnostic: function(ev) {
		var textarea = document.querySelector('[data-edgepulse-agent-question]');
		var output = document.querySelector('[data-edgepulse-agent-output]');
		var message = textarea && textarea.value ? textarea.value : 'Run a local EdgePulse diagnostic.';

		if (output)
			output.textContent = _('Running diagnostic...');

		return fs.exec_direct('/usr/libexec/edgepulse-luci', [ 'agent-diagnose', message ])
			.then(function(result) {
				var parsed;

				try {
					parsed = JSON.parse(result || '{}');
					if (output)
						output.textContent = JSON.stringify(parsed, null, 2);
				} catch (e) {
					if (output)
						output.textContent = result || _('No output');
				}
			})
			.catch(function(err) {
				ui.addNotification(null, E('p', {}, String(err)), 'danger');
				if (output)
					output.textContent = String(err);
			});
	},

	render: function(data) {
		var status = {};
		var agent = {};
		var model = {};

		try {
			status = JSON.parse(data || '{}');
		} catch (e) {
			status = { error: _('Unable to parse EdgePulse agent status output') };
		}

		if (status.error)
			ui.addNotification(null, E('p', {}, status.error), 'danger');

		agent = status.agent || {};
		model = status.model || {};

		return E('div', { 'class': 'cbi-map' }, [
			E('h2', {}, _('EdgePulse AI Agent')),
			E('div', { 'class': 'table' }, [
				E('div', { 'class': 'tr' }, [
					E('div', { 'class': 'td left' }, _('Status')),
					E('div', { 'class': 'td left' }, status.status || '-')
				]),
				E('div', { 'class': 'tr' }, [
					E('div', { 'class': 'td left' }, _('Enabled')),
					E('div', { 'class': 'td left' }, agent.enabled ? _('Yes') : _('No'))
				]),
				E('div', { 'class': 'tr' }, [
					E('div', { 'class': 'td left' }, _('Policy')),
					E('div', { 'class': 'td left' }, agent.policy_profile || '-')
				]),
				E('div', { 'class': 'tr' }, [
					E('div', { 'class': 'td left' }, _('Local only')),
					E('div', { 'class': 'td left' }, agent.local_only ? _('Yes') : _('No'))
				]),
				E('div', { 'class': 'tr' }, [
					E('div', { 'class': 'td left' }, _('Model configured')),
					E('div', { 'class': 'td left' }, model.configured ? _('Yes') : _('No'))
				]),
				E('div', { 'class': 'tr' }, [
					E('div', { 'class': 'td left' }, _('Model')),
					E('div', { 'class': 'td left' }, model.model || '-')
				]),
				E('div', { 'class': 'tr' }, [
					E('div', { 'class': 'td left' }, _('Base URL')),
					E('div', { 'class': 'td left' }, model.base_url || '-')
				])
			]),
			E('h3', {}, _('Diagnostic')),
			E('div', { 'class': 'cbi-section' }, [
				E('textarea', {
					'data-edgepulse-agent-question': '1',
					'style': 'width:100%; min-height:6em'
				}, [ _('Check the router health with local read-only tools.') ]),
				E('div', { 'class': 'right' }, [
					E('button', {
						'class': 'btn cbi-button cbi-button-action',
						'click': this.runDiagnostic.bind(this)
					}, [ _('Run') ])
				]),
				E('pre', {
					'data-edgepulse-agent-output': '1',
					'style': 'white-space:pre-wrap; margin-top:1em'
				}, '')
			])
		]);
	}
});
