'use strict';
'require view';
'require fs';
'require ui';

function renderRows(features) {
	return features.map(function(feature) {
		return E('tr', {}, [
			E('td', {}, feature.metric || '-'),
			E('td', {}, feature.labels || ''),
			E('td', {}, feature.count != null ? feature.count : '-'),
			E('td', {}, feature.mean != null ? '%.6f'.format(feature.mean) : '-'),
			E('td', {}, feature.min != null ? '%.6f'.format(feature.min) : '-'),
			E('td', {}, feature.max != null ? '%.6f'.format(feature.max) : '-')
		]);
	});
}

return view.extend({
	handleSaveApply: null,
	handleSave: null,
	handleReset: null,

	load: function() {
		return fs.exec_direct('/usr/bin/edgepulse-ctl', [ 'features', '--json', '--window', '60' ])
			.catch(function(err) {
				return JSON.stringify({ error: String(err), features: [] });
			});
	},

	render: function(data) {
		var parsed = {};
		var rows;

		try {
			parsed = JSON.parse(data || '{}');
		} catch (e) {
			parsed = { error: _('Unable to parse EdgePulse features output'), features: [] };
		}

		if (parsed.error)
			ui.addNotification(null, E('p', {}, parsed.error), 'danger');

		rows = [
			E('tr', { 'class': 'tr table-titles' }, [
				E('th', { 'class': 'th' }, _('Metric')),
				E('th', { 'class': 'th' }, _('Labels')),
				E('th', { 'class': 'th' }, _('Count')),
				E('th', { 'class': 'th' }, _('Mean')),
				E('th', { 'class': 'th' }, _('Min')),
				E('th', { 'class': 'th' }, _('Max'))
			])
		].concat(renderRows(parsed.features || []));

		return E('div', { 'class': 'cbi-map' }, [
			E('h2', {}, _('EdgePulse Features')),
			E('table', { 'class': 'table' }, rows)
		]);
	}
});
