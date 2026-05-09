'use strict';
'require view';
'require fs';
'require ui';

return view.extend({
	handleSaveApply: null,
	handleSave: null,
	handleReset: null,

	load: function() {
		return Promise.all([
			fs.exec_direct('/usr/libexec/edgepulse-luci', [ 'status' ])
				.catch(function(err) {
					return JSON.stringify({ error: String(err) });
				}),
			fs.exec_direct('/usr/libexec/edgepulse-luci', [ 'latest' ])
				.catch(function(err) {
					return JSON.stringify({ error: String(err), samples: [] });
				})
		]);
	},

	render: function(data) {
		var parsed = {};
		var latest = {};
		var samples = [];
		var sampleMap = {};
		var collectorRows = [];
		var networkRows = [];
		var thermalRows = [];

		function sampleKey(sample) {
			return '%s|%s'.format(sample.metric || '', sample.labels || '');
		}

		function sampleValue(metric, labels) {
			var sample = sampleMap['%s|%s'.format(metric, labels || '')];

			return sample && sample.value != null ? sample.value : null;
		}

		function renderValue(value, suffix) {
			if (value == null)
				return '-';

			return suffix ? '%.2f %s'.format(value, suffix) : '%.2f'.format(value);
		}

		try {
			parsed = JSON.parse((data && data[0]) || '{}');
		} catch (e) {
			parsed = { error: _('Unable to parse EdgePulse status output') };
		}

		try {
			latest = JSON.parse((data && data[1]) || '{}');
		} catch (e) {
			latest = { error: _('Unable to parse EdgePulse latest metrics output'), samples: [] };
		}

		if (parsed.error)
			ui.addNotification(null, E('p', {}, parsed.error), 'danger');
		if (latest.error)
			ui.addNotification(null, E('p', {}, latest.error), 'danger');

		var memory = parsed.memory || {};
		var load = parsed.load || {};

		samples = latest.samples || [];
		samples.forEach(function(sample) {
			sampleMap[sampleKey(sample)] = sample;

			if ((sample.metric || '').indexOf('collector.') === 0) {
				collectorRows.push(E('tr', {}, [
					E('td', {}, sample.metric.replace(/^collector\./, '')),
					E('td', {}, sample.status || '-')
				]));
			} else if ((sample.metric || '').indexOf('network.') === 0) {
				networkRows.push(E('tr', {}, [
					E('td', {}, sample.metric.replace(/^network\./, '')),
					E('td', {}, sample.labels || ''),
					E('td', {}, sample.value != null ? '%.0f'.format(sample.value) : '-')
				]));
			} else if ((sample.metric || '').indexOf('thermal.') === 0) {
				thermalRows.push(E('tr', {}, [
					E('td', {}, sample.labels || ''),
					E('td', {}, renderValue(sample.value, 'C'))
				]));
			}
		});

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
				]),
				E('div', { 'class': 'tr' }, [
					E('div', { 'class': 'td left' }, _('CPU user/system/idle')),
					E('div', { 'class': 'td left' }, '%s / %s / %s'.format(
						renderValue(sampleValue('cpu.user_jiffies', '')),
						renderValue(sampleValue('cpu.system_jiffies', '')),
						renderValue(sampleValue('cpu.idle_jiffies', ''))))
				]),
				E('div', { 'class': 'tr' }, [
					E('div', { 'class': 'td left' }, _('Conntrack')),
					E('div', { 'class': 'td left' }, renderValue(sampleValue('network.conntrack_count', '')))
				])
			]),
			E('h3', {}, _('Thermal')),
			E('table', { 'class': 'table' }, [
				E('tr', { 'class': 'tr table-titles' }, [
					E('th', { 'class': 'th' }, _('Zone')),
					E('th', { 'class': 'th' }, _('Temperature'))
				])
			].concat(thermalRows.length ? thermalRows : [
				E('tr', {}, [ E('td', { 'colspan': 2 }, _('No thermal samples')) ])
			])),
			E('h3', {}, _('Network')),
			E('table', { 'class': 'table' }, [
				E('tr', { 'class': 'tr table-titles' }, [
					E('th', { 'class': 'th' }, _('Metric')),
					E('th', { 'class': 'th' }, _('Labels')),
					E('th', { 'class': 'th' }, _('Value'))
				])
			].concat(networkRows.length ? networkRows : [
				E('tr', {}, [ E('td', { 'colspan': 3 }, _('No network samples')) ])
			])),
			E('h3', {}, _('Collectors')),
			E('table', { 'class': 'table' }, [
				E('tr', { 'class': 'tr table-titles' }, [
					E('th', { 'class': 'th' }, _('Collector')),
					E('th', { 'class': 'th' }, _('Status'))
				])
			].concat(collectorRows.length ? collectorRows : [
				E('tr', {}, [ E('td', { 'colspan': 2 }, _('All collectors OK')) ])
			]))
		]);
	}
});
