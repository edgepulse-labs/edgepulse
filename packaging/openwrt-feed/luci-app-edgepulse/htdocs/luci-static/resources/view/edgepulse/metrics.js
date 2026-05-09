'use strict';
'require view';
'require fs';
'require ui';

function renderRows(samples) {
	return samples.map(function(sample) {
		return E('tr', {}, [
			E('td', {}, sample.timestamp || '-'),
			E('td', {}, sample.metric || '-'),
			E('td', {}, sample.labels || ''),
			E('td', {}, sample.value != null ? '%.6f'.format(sample.value) : '-'),
			E('td', {}, sample.status || '-')
		]);
	});
}

return view.extend({
	handleSaveApply: null,
	handleSave: null,
	handleReset: null,

	load: function() {
		return fs.exec_direct('/usr/bin/edgepulse-ctl', [ 'latest', '--json' ])
			.catch(function(err) {
				return JSON.stringify({ error: String(err), samples: [] });
			});
	},

	render: function(data) {
		var parsed = {};
		var rows;

		try {
			parsed = JSON.parse(data || '{}');
		} catch (e) {
			parsed = { error: _('Unable to parse EdgePulse metrics output'), samples: [] };
		}

		if (parsed.error)
			ui.addNotification(null, E('p', {}, parsed.error), 'danger');

		rows = [
			E('tr', { 'class': 'tr table-titles' }, [
				E('th', { 'class': 'th' }, _('Timestamp')),
				E('th', { 'class': 'th' }, _('Metric')),
				E('th', { 'class': 'th' }, _('Labels')),
				E('th', { 'class': 'th' }, _('Value')),
				E('th', { 'class': 'th' }, _('Status'))
			])
		].concat(renderRows(parsed.samples || []));

		return E('div', { 'class': 'cbi-map' }, [
			E('h2', {}, _('EdgePulse Metrics')),
			E('table', { 'class': 'table' }, rows)
		]);
	}
});
