(() => {
  'use strict';
  let latest = null,
    sortKey = 'ip',
    ascending = true,
    receivedAt = 0,
    uptimeBase = 0,
    clockBase = 0,
    lastScan = 0,
    busy = false,
    requesting = false;
  const $ = id => document.getElementById(id);
  let timeFormat, dateFormat, zone = '',
    formatKey = '';

  function configureTime(d) {
    const requestedZone = d.timeZone || Intl.DateTimeFormat().resolvedOptions().timeZone;
    const format = d.timeFormat === '12' ? '12' : '24';
    const key = requestedZone + '|' + format;
    if (key === formatKey) return;
    formatKey = key;
    zone = requestedZone;
    try {
      new Intl.DateTimeFormat(undefined, {
        timeZone: zone
      })
    } catch (e) {
      zone = 'UTC'
    }
    timeFormat = new Intl.DateTimeFormat(undefined, {
      timeZone: zone,
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
      hourCycle: format === '12' ? 'h12' : 'h23'
    });
    dateFormat = new Intl.DateTimeFormat(undefined, {
      timeZone: zone,
      weekday: 'short',
      month: 'short',
      day: 'numeric',
      year: 'numeric',
      timeZoneName: 'short'
    });
    $('zoneLabel').textContent = 'Time zone: ' + zone + ' / ' + format + '-hour time'
  }
  const stateText = v => v.connection || 'Not reported';
  const reducedMotion = () => window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  function relative(seconds) {
    seconds = Math.max(0, Math.floor(seconds));
    return seconds < 60 ? seconds + ' sec ago' : seconds < 3600 ? Math.floor(seconds / 60) + ' min ago' : Math.floor(seconds / 3600) + ' hr ago'
  }

  function matches(v) {
    const group = $('deviceGroup').value;
    if (group !== 'all' && (v.status !== (group.startsWith('offline') ? 'OFFLINE' : 'ONLINE') ||
        group.endsWith('-authorized') && v.policy !== 'Authorized')) return false;
    const choice = $('statusFilter').value;
    if (choice === 'online' && v.status !== 'ONLINE' || choice === 'offline' && v.status !== 'OFFLINE' || choice === 'changed' && !v.changed || choice === 'new' && !v.isNew) return false;
    if (choice === 'authorized' && (v.status !== 'ONLINE' || v.policy !== 'Authorized')) return false;
    if (choice === 'unauthorized' && (v.status !== 'ONLINE' || v.policy === 'Authorized')) return false;
    const text = [v.policy, v.ip, v.name || 'Unknown', v.hostname, v.mac || 'Unknown', v.status, stateText(v), v.changed ? 'changed' : '', v.isNew ? 'new' : ''].join(' ').toLowerCase();
    return $('search').value.trim().toLowerCase().split(/\s+/).every(term => text.includes(term))
  }

  function renderHeatmap() {
    const grid = $('deviceHeatmap');
    const focused = grid.contains(document.activeElement) ? document.activeElement.dataset.mac : null;
    grid.replaceChildren();
    const devices = [...latest.devices].sort((a, b) => deviceSort.compare(a, b, 'ip', true));
    $('heatmapCount').textContent = devices.length + ' tracked devices';
    if (!devices.length) {
      grid.textContent = latest.completed ? 'No devices discovered yet.' : 'Waiting for the first scan.';
      return;
    }
    for (const device of devices) {
      const online = device.status === 'ONLINE';
      const state = !online ? 'offline' : device.policy === 'Authorized' ? 'online'
        : device.policy === 'Acknowledged' ? 'ack' : 'alert';
      const label = [device.name || device.hostname || 'Unknown device', device.ip || 'IP unavailable',
        device.mac || 'MAC unavailable', online ? 'Online' : 'Offline', device.policy].join(' · ');
      const tile = document.createElement('button');
      tile.type = 'button';
      tile.className = 'heat-tile heat-' + state;
      tile.dataset.mac = device.mac || device.ip;
      tile.title = label;
      tile.setAttribute('aria-label', label);
      tile.addEventListener('focus', () => { $('heatmapDetail').textContent = label; });
      tile.addEventListener('mouseenter', () => { $('heatmapDetail').textContent = label; });
      tile.addEventListener('click', () => {
        $('deviceGroup').value = 'all';
        $('statusFilter').value = 'all';
        $('search').value = device.mac || device.ip || device.name || '';
        render();
        $('device-inventory').scrollIntoView({behavior: reducedMotion() ? 'auto' : 'smooth', block: 'start'});
      });
      grid.append(tile);
      if (focused && focused === tile.dataset.mac) tile.focus({preventScroll: true});
    }
  }

  function render() {
    if (!latest) return;
    for (const link of document.querySelectorAll('[data-filter]')) {
      const selected = $('deviceGroup').value === 'all' && link.dataset.filter === $('statusFilter').value;
      link.classList.toggle('selected', selected);
      link.setAttribute('aria-current', String(selected))
    }
    const shown = latest.devices.filter(matches).sort((a, b) => deviceSort.compare(a, b, sortKey, ascending));
    const rows = $('rows');
    rows.replaceChildren();
    for (const v of shown) {
      const tr = document.createElement('tr');
      let td = document.createElement('td');
      td.className = 'mono';
      td.textContent = v.ip || 'Unknown';
      tr.appendChild(td);
      td = document.createElement('td');
      td.className = 'device-name' + (v.name ? '' : ' unknown');
      td.textContent = v.name || 'Unknown';
      td.title = v.name || 'Unknown';
      tr.appendChild(td);
      td = document.createElement('td');
      td.textContent = v.hostname || '—';
      tr.appendChild(td);
      td = document.createElement('td');
      td.className = 'mono muted';
      td.textContent = v.mac || 'Unknown';
      tr.appendChild(td);
      td = document.createElement('td');
      const group = document.createElement('div');
      group.className = 'status-cell';
      const badge = document.createElement('span');
      badge.className = 'badge ' + (v.status === 'ONLINE' ? 'online' : 'offline');
      const dot = document.createElement('span');
      dot.className = 'dot';
      badge.appendChild(dot);
      badge.appendChild(document.createTextNode(v.status));
      group.appendChild(badge);
      const policy = document.createElement('span');
      policy.className = 'badge ' + (v.policy === 'Authorized' ? 'authorized'
        : v.policy === 'Acknowledged' ? 'acknowledged' : 'unauthorized');
      policy.textContent = v.policy;
      group.appendChild(policy);
      for (const tag of [v.isNew ? 'New' : v.changed ? 'Changed' : '']) {
        if (tag) {
          const label = document.createElement('span');
          label.className = 'change-tag';
          label.textContent = tag;
          group.appendChild(label)
        }
      }
      td.appendChild(group);
      tr.appendChild(td);
      td = document.createElement('td');
      td.className = 'muted';
      td.textContent = stateText(v);
      tr.appendChild(td);
      td = document.createElement('td');
      td.className = 'device-actions';
      if (v.policyAvailable) {
        const actions = v.policy === 'Authorized'
          ? [['reset', 'Deauthorize']]
          : [['allow', 'Authorize'], ['acknowledge', 'Acknowledge']];
        for (const [action, label] of actions) {
          if (action === 'acknowledge' && !v.canAcknowledge) continue;
          const link = document.createElement('a');
          link.className = 'btn';
          link.href = '/admin?' + new URLSearchParams({device: v.mac, action}) + '#devices';
          link.textContent = label;
          td.append(link);
        }
      } else td.textContent = 'MAC unavailable';
      tr.append(td);
      tr.setAttribute('role', 'row');
      for (const [i, label] of ['IP address', 'Device name', 'DNS name', 'MAC address', 'Status', 'Connection', 'Actions'].entries()) {
        tr.children[i].dataset.label = label;
        tr.children[i].setAttribute('role', 'cell')
      }
      rows.appendChild(tr)
    }
    if (!shown.length) {
      const tr = document.createElement('tr'),
        td = document.createElement('td');
      td.colSpan = 7;
      td.className = 'empty';
      const title = document.createElement('strong');
      title.textContent = latest.devices.length ? 'No matching devices' : 'No devices discovered yet';
      td.appendChild(title);
      td.appendChild(document.createTextNode(latest.devices.length ? 'Try a different search or status filter.' : 'The list will update after a successful scan.'));
      tr.appendChild(td);
      rows.appendChild(tr)
    }
    $('resultCount').textContent = 'Showing ' + shown.length + ' of ' + latest.devices.length + ' devices';
    $('clearSearch').hidden = !$('search').value
  }

  function clockTick() {
    if (!latest) return;
    const elapsed = (performance.now() - receivedAt) / 1000;
    const up = Math.max(0, Math.floor(uptimeBase + elapsed)),
      days = Math.floor(up / 86400),
      hours = Math.floor(up % 86400 / 3600),
      minutes = Math.floor(up % 3600 / 60),
      seconds = up % 60;
    $('boardUptime').textContent = (days ? days + 'd ' : '') + [hours, minutes, seconds].map(v => String(v).padStart(2, '0')).join(':');
    $('uptimeDetail').textContent = elapsed > 15 ? 'From the last monitor connection' : 'Since the system last restarted';
    if (clockBase) {
      const current = new Date((clockBase + elapsed) * 1000);
      $('currentTime').textContent = timeFormat.format(current);
      $('currentDate').textContent = dateFormat.format(current);
      $('clockStatus').textContent = elapsed > 15 ? 'Clock from last connection' : 'Time synchronized';
      $('clockState').className = 'badge online clock-badge'
    } else {
      $('currentTime').textContent = '--:--:--';
      $('currentDate').textContent = 'Waiting for time synchronization';
      $('clockStatus').textContent = 'Synchronizing with ' + (latest.timeServer || 'time.nist.gov');
      $('clockState').className = 'badge pending clock-badge'
    }
    if (lastScan) {
      const last = new Date(lastScan * 1000);
      $('lastTime').textContent = timeFormat.format(last);
      $('lastDate').textContent = dateFormat.format(last)
    } else {
      $('lastTime').textContent = latest.completed ? 'Scan complete' : 'Not scanned yet';
      $('lastDate').textContent = latest.completed ? 'Timestamp available after clock sync' : 'Your first report will appear here'
    }
    $('nextScan').textContent = !latest.ethernetIP ? 'Waiting for Ethernet'
      : latest.scanning || latest.scanRequested ? 'Scanning'
      : 'Next scan in ' + Math.max(0, Math.ceil(latest.nextScanSeconds - elapsed)) + ' sec';
    $('scanAge').textContent = latest.completed ? 'Completed ' + relative(latest.age + elapsed) : 'Waiting for first scan'
  }

  function scanButton() {
    const scanning = latest && (latest.scanning || latest.scanRequested);
    $('scanNow').disabled = !latest || scanning || requesting;
    $('scanButtonLabel').textContent = scanning ? 'Scanning…' : requesting ? 'Starting…' : 'Scan now';
    $('scanIcon').classList.toggle('spinner', Boolean(scanning || requesting))
  }
  async function refresh() {
    if (busy) return;
    busy = true;
    try {
      const response = await fetch('/api/devices', {
        cache: 'no-store'
      });
      if (!response.ok) throw Error('Monitor is busy. Retrying shortly.');
      const d = await response.json();
      latest = d;
      configureTime(d);
      receivedAt = performance.now();
      uptimeBase = Number(d.uptimeSeconds) || 0;
      clockBase = d.clockSynced ? d.nowEpoch : 0;
      lastScan = d.lastScanEpoch || 0;
      $('networkName').textContent = d.ethernetIP ? 'Ethernet · ' + d.ethernetIP : d.apName || 'Watchtower';
      document.title = 'Watchtower - Intrusion Detection System (IDS)';
      $('error').hidden = !d.error;
      $('error').textContent = d.error || '';
      const scanning = d.scanning || d.scanRequested;
      $('summary').textContent = d.error ? 'Scan needs attention' : scanning ? 'Scan in progress' : d.ethernetIP ? 'Monitoring every ' + d.interval + 's' : 'Waiting for Ethernet';
      $('scanState').className = 'badge ' + (d.error ? 'offline' : scanning ? 'pending' : 'online');
      for (const [id, value] of [
          ['countOnline', d.online],
          ['countOffline', d.offline],
          ['countNew', d.devices.filter(v => v.status === 'ONLINE' && v.policy === 'Authorized').length],
          ['countChanged', d.devices.filter(v => v.status === 'ONLINE' && v.policy !== 'Authorized').length],
          ['countTotal', d.devices.length]
        ]) $(id).textContent = d.completed ? value : '--';
      $('sourceLabel').textContent = d.clientMode ? 'GL.iNet live client list' : d.source === 'openwrt' ? 'OpenWrt router inventory' : 'Local network discovery';
      $('note').textContent = d.clientMode ? 'Connected and disconnected status comes from the GL.iNet client tracker. Device names use the router’s aliases and hostnames.' : 'Device status reflects the latest successful discovery scan. Unresponsive devices can appear OFFLINE.';
      $('refreshInfo').textContent = 'Scans every ' + d.interval + 's / page updates every 5s';
      clockTick();
      scanButton();
      renderHeatmap();
      render()
    } catch (e) {
      $('error').hidden = false;
      $('error').textContent = e.message || 'Monitor unreachable. Retrying…';
      $('summary').textContent = 'Connection interrupted';
      $('scanState').className = 'badge offline'
    } finally {
      busy = false
    }
  }
  for (const link of document.querySelectorAll('[data-filter]')) link.addEventListener('click', event => {
    event.preventDefault();
    $('deviceGroup').value = 'all';
    $('statusFilter').value = link.dataset.filter;
    $('search').value = '';
    render();
    $('device-inventory').scrollIntoView({
      behavior: reducedMotion() ? 'auto' : 'smooth',
      block: 'start'
    })
  });
  $('deviceGroup').addEventListener('change', render);
  $('search').addEventListener('input', render);
  $('statusFilter').addEventListener('change', render);
  $('clearSearch').addEventListener('click', () => {
    $('search').value = '';
    $('search').focus();
    render()
  });
  for (const button of document.querySelectorAll('th button')) button.addEventListener('click', () => {
    ascending = sortKey === button.dataset.key ? !ascending : true;
    sortKey = button.dataset.key;
    for (const b of document.querySelectorAll('th button')) deviceSort.mark(b, sortKey, ascending);
    render()
  });
  $('scanNow').addEventListener('click', async () => {
    if (!latest || requesting) return;
    requesting = true;
    scanButton();
    try {
      const r = await fetch('/api/scan', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/x-www-form-urlencoded'
        },
        body: 'token=' + encodeURIComponent(latest.scanToken)
      });
      const d = await r.json();
      if (!r.ok) throw Error(d.error || 'Could not start the scan.');
      latest.scanRequested = true;
      scanButton();
      await refresh()
    } catch (e) {
      $('error').hidden = false;
      $('error').textContent = e.message
    } finally {
      requesting = false;
      scanButton()
    }
  });
  refresh();
  setInterval(refresh, 5000);
  setInterval(clockTick, 1000);
})();
