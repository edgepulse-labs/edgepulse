'use strict';
'require view';
'require fs';
'require ui';

return view.extend({
	load: function() {
		return Promise.all([
			fs.exec_direct('/usr/libexec/edgepulse-luci', [ 'agent-status' ])
				.catch(function(err) {
					return JSON.stringify({ error: String(err) });
				}),
			fs.exec_direct('/usr/libexec/edgepulse-luci', [ 'agent-memory' ])
				.catch(function(err) {
					return JSON.stringify({ error: String(err), memory: [] });
				})
		]);
	},

	runDiagnostic: function(ev) {
		var textarea = document.querySelector('[data-edgepulse-agent-question]');
		var output = document.querySelector('[data-edgepulse-agent-output]');
		var message = ev && ev.currentTarget && ev.currentTarget.getAttribute('data-edgepulse-agent-prompt') ||
			textarea && textarea.value ||
			'Run a local EdgePulse diagnostic.';

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
		var memory = {};

		try {
			status = JSON.parse((data && data[0]) || '{}');
		} catch (e) {
			status = { error: _('Unable to parse EdgePulse agent status output') };
		}

		try {
			memory = JSON.parse((data && data[1]) || '{}');
		} catch (e) {
			memory = { error: _('Unable to parse EdgePulse agent memory output'), memory: [] };
		}

		if (status.error)
			ui.addNotification(null, E('p', {}, status.error), 'danger');
		if (memory.error)
			ui.addNotification(null, E('p', {}, memory.error), 'danger');

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
				]),
				E('div', { 'class': 'tr' }, [
					E('div', { 'class': 'td left' }, _('Memory records')),
					E('div', { 'class': 'td left' }, String((memory.memory || []).length))
				])
			]),
			E('h3', {}, _('Diagnostic')),
			E('div', { 'class': 'cbi-section' }, [
				E('textarea', {
					'data-edgepulse-agent-question': '1',
					'style': 'width:100%; min-height:6em'
				}, [ _('Check the router health with local read-only tools.') ]),
				E('div', { 'class': 'cbi-section-actions' }, [
					E('button', {
						'class': 'btn cbi-button',
						'data-edgepulse-agent-prompt': 'Diagnose WAN connectivity using local read-only status.',
						'click': this.runDiagnostic.bind(this)
					}, [ _('WAN') ]),
					' ',
					E('button', {
						'class': 'btn cbi-button',
						'data-edgepulse-agent-prompt': 'Diagnose DNS health using local read-only status.',
						'click': this.runDiagnostic.bind(this)
					}, [ _('DNS') ]),
					' ',
					E('button', {
						'class': 'btn cbi-button',
						'data-edgepulse-agent-prompt': 'Diagnose Wi-Fi instability using local read-only status.',
						'click': this.runDiagnostic.bind(this)
					}, [ _('Wi-Fi') ]),
					' ',
					E('button', {
						'class': 'btn cbi-button',
						'data-edgepulse-agent-prompt': 'Diagnose high CPU or high memory pressure using local read-only status.',
						'click': this.runDiagnostic.bind(this)
					}, [ _('Load') ]),
					' ',
					E('button', {
						'class': 'btn cbi-button',
						'data-edgepulse-agent-prompt': 'Diagnose package and service health using local read-only status.',
						'click': this.runDiagnostic.bind(this)
					}, [ _('Services') ])
				]),
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
