'use strict';
'require view';
'require fs';
'require ui';

return view.extend({
	handleSaveApply: null,
	handleSave: null,
	handleReset: null,

	load: function() {
		return fs.exec_direct('/usr/bin/edgepulse-ctl', [ 'status', '--json' ])
			.catch(function(err) {
				return JSON.stringify({ error: String(err) });
			});
	},

	render: function(data) {
		var parsed = {};

		try {
			parsed = JSON.parse(data || '{}');
		} catch (e) {
			parsed = { error: _('Unable to parse EdgePulse status output') };
		}

		if (parsed.error)
			ui.addNotification(null, E('p', {}, parsed.error), 'danger');

		var memory = parsed.memory || {};
		var load = parsed.load || {};

		return E('div', { 'class': 'cbi-map' }, [
			E('h2', {}, _('EdgePulse')),
			E('div', { 'class': 'table' }, [
				E('div', { 'class': 'tr' }, [
					E('div', { 'class': 'td left' }, _('Version')),
					E('div', { 'class': 'td left' }, parsed.version || '-')
				]),
				E('div', { 'class': 'tr' }, [
					E('div', { 'class': 'td left' }, _('Timestamp')),
					E('div', { 'class': 'td left' }, parsed.timestamp || '-')
				]),
				E('div', { 'class': 'tr' }, [
					E('div', { 'class': 'td left' }, _('Uptime')),
					E('div', { 'class': 'td left' }, parsed.uptime_sec != null ? '%.2f s'.format(parsed.uptime_sec) : '-')
				]),
				E('div', { 'class': 'tr' }, [
					E('div', { 'class': 'td left' }, _('Load')),
					E('div', { 'class': 'td left' }, '%s / %s / %s'.format(load['1m'] || '-', load['5m'] || '-', load['15m'] || '-'))
				]),
				E('div', { 'class': 'tr' }, [
					E('div', { 'class': 'td left' }, _('Memory used')),
					E('div', { 'class': 'td left' }, memory.used_ratio != null ? '%.2f%%'.format(memory.used_ratio * 100) : '-')
				])
			])
		]);
	}
});
