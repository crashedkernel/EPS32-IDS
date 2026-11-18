(() => {
  'use strict';
  const panel = document.getElementById('adminDevices');
  const feedback = document.getElementById('deviceFeedback');
  const search = document.getElementById('deviceSearch');
  let rows = [], sortKey = 'ip', ascending = true;
  const selected = new URLSearchParams(location.search).get('device');
  const requested = new URLSearchParams(location.search).get('action');
  const selectedMacs = new Set();
  const selectAll = document.getElementById('selectAllDevices');
  const selectNone = document.getElementById('selectNoDevices');
  const authorize = document.getElementById('authorizeSelected');
  const deauthorize = document.getElementById('deauthorizeSelected');
  const clearSelected = document.getElementById('clearSelectedDevices');
  const selectionCount = document.getElementById('deviceSelectionCount');
  let visibleMacs = [];

  function updateSelectionControls() {
    selectAll.disabled = busy || !visibleMacs.some(mac => !selectedMacs.has(mac));
    selectNone.disabled = busy || !selectedMacs.size;
    authorize.disabled = deauthorize.disabled = clearSelected.disabled = busy || !selectedMacs.size;
    selectionCount.textContent = selectedMacs.size + ' selected';
  }

  async function savePolicy(action, mac = '') {
    const response = await fetch('/admin/devices', {
      method: 'POST',
      body: new URLSearchParams({csrf: panel.dataset.token, action, mac})
    });
    const message = await response.text();
    if (!response.ok) throw new Error(message);
    return message;
  }
  let busy = false;
  let signature = '';
  let refreshing = false;

  async function update(action, mac = '') {
    if (busy) return;
    busy = true;
    updateSelectionControls();
    try {
      const message = await savePolicy(action, mac);
      feedback.textContent = action === 'clear'
        ? 'History cleared. Saved policies will apply when devices are rediscovered.'
        : action === 'acknowledge-all' ? message : 'Device policy saved.';
    } catch (error) {
      feedback.textContent = error.message;
    } finally {
      busy = false;
      updateSelectionControls();
      refresh();
    }
  }

  async function updateSelected(action) {
    if (busy || !selectedMacs.size) return;
    busy = true;
    updateSelectionControls();
    let saved = 0;
    let failed = 0;
    for (const mac of [...selectedMacs]) {
      try {
        await savePolicy(action, mac);
        selectedMacs.delete(mac);
        saved++;
      } catch {
        failed++;
      }
    }
    busy = false;
    const clearing = action === 'clear-device';
    const verb = clearing ? 'Cleared ' : action === 'allow' ? 'Authorized ' : 'Deauthorized ';
    feedback.textContent = verb + saved + (saved === 1 ? ' device' : ' devices') + (clearing ? ' from history.' : '.') +
      (failed ? ' Could not update ' + failed + '. Failed devices remain selected.' : '');
    render();
    refresh();
  }

  async function refresh() {
    if (busy || refreshing) return;
    refreshing = true;
    try {
      const response = await fetch('/api/devices', {cache: 'no-store'});
      if (!response.ok) throw new Error('Device list unavailable.');
      const report = await response.json();
      if (busy) return;
      const next = JSON.stringify(report.devices.map(d => [d.mac, d.ip, d.name, d.hostname, d.status, d.policy, d.policyAvailable, d.canAcknowledge]));
      if (next === signature) return;
      signature = next;
      rows = report.devices;
      const available = new Set(rows.filter(device => device.policyAvailable).map(device => device.mac));
      for (const mac of selectedMacs) if (!available.has(mac)) selectedMacs.delete(mac);
      render();
    } catch (error) {
      feedback.textContent = error.message;
    } finally {
      refreshing = false;
    }
  }

  function render() {
    const terms = search.value.trim().toLowerCase().split(/\s+/);
    const group = document.getElementById('adminDeviceGroup').value;
    const devices = rows.filter(d => {
      if (group !== 'all' && (d.status !== (group.startsWith('offline') ? 'OFFLINE' : 'ONLINE') ||
          group.endsWith('-authorized') && d.policy !== 'Authorized')) return false;
      const text = [d.name || 'Unknown', d.hostname, d.ip, d.mac, d.status, d.policy].join(' ').toLowerCase();
      return terms.every(term => text.includes(term));
    }).sort((a, b) => deviceSort.compare(a, b, sortKey, ascending));
    visibleMacs = devices.filter(device => device.policyAvailable).map(device => device.mac);
    updateSelectionControls();
    panel.replaceChildren();
    if (!rows.length) {
      panel.textContent = 'No devices discovered yet.';
      return;
    }
    const scroller = document.createElement('div');
    scroller.className = 'table-scroll device-table';
    const table = document.createElement('table');
    const header = table.createTHead().insertRow();
    const selectHeader = document.createElement('th');
    selectHeader.scope = 'col';
    selectHeader.className = 'selection-column';
    selectHeader.textContent = 'Select';
    header.append(selectHeader);
    const labels = ['Device name', 'DNS name', 'IP address', 'MAC address', 'Presence', 'Policy', 'Actions'];
    const keys = ['name', 'hostname', 'ip', 'mac', 'presence', 'policy', 'actions'];
    for (const [index, label] of labels.entries()) {
      const th = document.createElement('th');
      th.scope = 'col';
      const button = document.createElement('button');
      button.type = 'button';
      button.dataset.key = keys[index];
      button.textContent = label + ' ';
      const marker = document.createElement('span');
      marker.className = 'sort-marker';
      marker.setAttribute('aria-hidden', 'true');
      button.append(marker);
      th.append(button);
      header.append(th);
      deviceSort.mark(button, sortKey, ascending);
      button.addEventListener('click', () => {
        ascending = sortKey === keys[index] ? !ascending : true;
        sortKey = keys[index];
        render();
        panel.querySelector('[data-key="' + sortKey + '"]').focus();
      });
    }
    const body = table.createTBody();
    for (const d of devices) {
      const row = body.insertRow();
      if (selected && d.mac === selected) row.className = 'selected-device';
      const selection = row.insertCell();
      selection.className = 'selection-column';
      selection.dataset.label = 'Select';
      if (d.policyAvailable) {
        const checkbox = document.createElement('input');
        checkbox.type = 'checkbox';
        checkbox.checked = selectedMacs.has(d.mac);
        checkbox.setAttribute('aria-label', 'Select ' + (d.name || d.ip || d.mac));
        checkbox.addEventListener('change', () => {
          if (checkbox.checked) selectedMacs.add(d.mac);
          else selectedMacs.delete(d.mac);
          updateSelectionControls();
        });
        selection.append(checkbox);
      }
      [d.name || 'Unknown', d.hostname || '—', d.ip || '—', d.mac || '—', d.status, d.policy].forEach((value, i) => {
        const cell = row.insertCell();
        cell.dataset.label = labels[i];
        cell.textContent = value;
        if (i === 5) cell.className = d.policy === 'Authorized' ? 'authorized'
          : d.policy === 'Acknowledged' ? 'acknowledged' : 'unauthorized';
      });
      const cell = row.insertCell();
      cell.dataset.label = 'Actions';
      cell.className = 'device-actions';
      if (!d.policyAvailable) {
        cell.textContent = 'MAC unavailable';
        continue;
      }
      const actions = d.policy === 'Authorized'
        ? [['reset', 'Deauthorize']]
        : [['allow', 'Authorize'], ['acknowledge', 'Acknowledge']];
      if (d.policy === 'Acknowledged') actions.push(['reset', 'Clear acknowledgment']);
      for (const [action, label] of actions) {
        if (action === 'acknowledge' && !d.canAcknowledge) continue;
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'btn' + (selected === d.mac && requested === action ? ' btn-primary' : '');
        button.textContent = label;
        button.addEventListener('click', () => update(action, d.mac));
        cell.append(button);
      }
    }
    scroller.append(table);
    panel.append(scroller);
    if (!devices.length) {
      const empty = body.insertRow().insertCell();
      empty.colSpan = labels.length + 1;
      empty.className = 'empty';
      empty.textContent = 'No matching devices.';
    }
  }

  selectAll.addEventListener('click', () => {
    for (const mac of visibleMacs) selectedMacs.add(mac);
    render();
  });
  selectNone.addEventListener('click', () => {
    selectedMacs.clear();
    render();
  });
  authorize.addEventListener('click', () => updateSelected('allow'));
  deauthorize.addEventListener('click', () => updateSelected('reset'));
  clearSelected.addEventListener('click', () => {
    if (!busy && selectedMacs.size && confirm('Clear the selected devices from history? The whitelist is kept.'))
      updateSelected('clear-device');
  });
  document.getElementById('acknowledgeAllDevices').addEventListener('click', () => update('acknowledge-all'));
  document.getElementById('adminDeviceGroup').addEventListener('change', render);
  search.addEventListener('input', render);
  document.getElementById('clearDevices').addEventListener('click', () => {
    if (confirm('Clear all device history? The whitelist is kept.')) update('clear');
  });
  refresh();
  setInterval(refresh, 5000);
})();
