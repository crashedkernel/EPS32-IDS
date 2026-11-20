(() => {
  'use strict';
  const $ = id => document.getElementById(id);

  async function adminFetch(url, options) {
    const response = await fetch(url, options);
    if (response.status === 401) {
      const banner = $('adminSessionAlert');
      const link = document.createElement('a');
      link.href = '/admin/login'; link.textContent = 'Sign In';
      banner.replaceChildren(document.createTextNode('Your Admin session has expired. '), link);
      banner.hidden = false;
    } else if (response.ok) $('adminSessionAlert').hidden = true;
    return response;
  }
  for (const input of document.querySelectorAll('[data-heat-speed]')) input.addEventListener('input', () => {
    document.querySelector('[data-heat-output="' + input.dataset.heatSpeed + '"]').textContent = (Number(input.value) / 10).toFixed(1) + ' flashes/s';
  });
  $('heatmapForm').addEventListener('submit', async event => {
    event.preventDefault();
    const button = event.target.querySelector('button[type=submit]');
    button.disabled = true;
    try {
      const body = new URLSearchParams(new FormData(event.target));
      body.set('csrf', $('settingsForm').elements.csrf.value);
      const response = await adminFetch('/admin/heatmap', {method: 'POST', body});
      if (!response.ok) throw Error(await response.text());
      watchtowerHeatmap.configure(await response.json());
      $('heatmapFeedback').textContent = 'Device Map settings saved for both pages.';
    } catch (error) { $('heatmapFeedback').textContent = error.message; }
    finally { button.disabled = false; }
  });
  async function recoveryStatus() {
    try {
      const response = await adminFetch('/admin/recovery', {cache: 'no-store'});
      if (!response.ok) throw Error('Recovery status is unavailable.');
      const state = await response.json();
      $('recoveryStatus').textContent = !state.enabled ? 'Automatic recovery is disabled.'
        : state.paused ? 'Recovery is paused after repeated restarts. Save enabled settings to resume.'
        : !state.ready ? 'Recovery monitor could not start. Retry or restart the system.'
        : state.graceSeconds ? 'Startup grace: ' + state.graceSeconds + ' seconds remaining.'
        : 'Monitoring the application and SSH operations.';
      if (state.lastRecovery !== 'None recorded') $('recoveryStatus').textContent += ' Last recovery: ' + state.lastRecovery + '.';
      if (state.enabled && state.sshBusy) $('recoveryStatus').textContent += ' SSH operation active for ' + state.sshAgeSeconds + ' seconds.';
    } catch (error) { $('recoveryStatus').textContent = error.message; }
  }
  $('recoveryForm').addEventListener('submit', async event => {
    event.preventDefault();
    const button = event.target.querySelector('button[type=submit]');button.disabled = true;
    try {
      const body = new URLSearchParams(new FormData(event.target));body.set('csrf', $('settingsForm').elements.csrf.value);
      const response = await adminFetch('/admin/recovery', {method:'POST', body});
      const message = await response.text();
      if (!response.ok) throw Error(message);
      $('recoveryFeedback').textContent = message;
      await recoveryStatus();
    } catch (error) { $('recoveryFeedback').textContent = error.message; }
    finally { button.disabled = false; }
  });
  recoveryStatus();
  setInterval(recoveryStatus, 5000);

  function addressFields() {
    $('subnetFields').hidden = $('mode').value !== 'subnet';
    $('rangeFields').hidden = $('mode').value !== 'range';
  }

  function timeFields() {
    const custom = $('timeServer').value === 'custom';
    $('customTimeServerField').hidden = !custom;
    const input = document.querySelector('[name=customTimeServer]');
    input.disabled = !custom;
    input.required = custom;
    try {
      const preview = new Intl.DateTimeFormat(undefined, {
        timeZone: $('timeZone').value,
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
        hourCycle: $('timeFormat').value === '12' ? 'h12' : 'h23',
        timeZoneName: 'short'
      });
      $('timePreview').textContent = 'Format preview: ' + preview.format(new Date());
    } catch (e) {
      $('timePreview').textContent = 'Choose a time zone supported by this browser.'
    }
  }
  function routerTransport() {
    const gl = $('source').value === 'glinet';
    $('transport').value = gl ? 'ssh' : 'http';
    $('transport').disabled = true;
    return gl;
  }
  $('source').addEventListener('change', () => {
    document.querySelector('[name=routerPort]').value = routerTransport() ? 22 : 80;
  });
  routerTransport();
  function ethernetFields() {
    const enabled = $('ipMode').value === 'static';
    $('staticFields').hidden = !enabled;
    for (const field of $('staticFields').querySelectorAll('input')) field.required = enabled;
  }
  $('ipMode').addEventListener('change', ethernetFields);
  ethernetFields();
  $('mode').addEventListener('change', addressFields);
  addressFields();
  for (const id of ['timeZone', 'timeFormat', 'timeServer']) $(id).addEventListener('change', timeFields);
  timeFields();
  (async () => {
    try {
      const response = await fetch('/assets/timezones.json');
      if (!response.ok) return;
      const zones = await response.json(),
        selected = $('timeZone').value;
      for (const zone of zones) {
        if (zone === selected) continue;
        try {
          new Intl.DateTimeFormat(undefined, {
            timeZone: zone
          })
        } catch (e) {
          continue
        }
        const option = document.createElement('option');
        option.value = zone;
        option.textContent = zone.replaceAll('_', ' ');
        $('timeZone').appendChild(option);
      }
    } catch (e) {}
  })();
  async function refreshAp() {
    try {
      const response = await adminFetch('/admin/ap', {cache: 'no-store'});
      if (!response.ok) return;
      const state = await response.json();
      if (state.pending) return;
      $('apStatus').textContent = state.running
        ? 'Running on channel ' + state.channel + ' (' + (2407 + 5 * state.channel) + ' MHz)' + (state.widthMHz ? ' · ' + state.widthMHz + ' MHz width' : '')
        : 'Stopped';
      $('apStatus').textContent += state.enabledAtBoot ? ' · Enabled at startup' : ' · Disabled at startup';
      $('apPersistent').dataset.enabled = String(state.enabledAtBoot);
      $('apPersistent').textContent = state.enabledAtBoot ? 'Disable AP Persistently' : 'Enable AP Persistently';
      $('apStart').disabled = state.running;
      $('apStop').disabled = !state.running;
    } catch {
      // Restarting the access point can temporarily disconnect this browser.
    }
  }
  for (const [id, command] of [['apStart', 'start'], ['apStop', 'stop'], ['apRestart', 'restart'], ['apPersistent', 'persistent']]) {
    $(id).addEventListener('click', async () => {
      const action = command === 'persistent'
        ? ($(id).dataset.enabled === 'true' ? 'disable' : 'enable')
        : command;
      const form = $('settingsForm');
      const body = new URLSearchParams({action, csrf: form.elements.csrf.value});
      if (action === 'restart') {
        body.set('name', form.elements.apName.value);
        body.set('password', form.elements.apPassword.value);
        body.set('channel', $('apChannel').value);
        body.set('width', $('apWidth').value);
      }
      $(id).disabled = true;
      try {
        const response = await adminFetch('/admin/ap', {method: 'POST', body});
        $('apStatus').textContent = await response.text();
        if (response.ok && action === 'restart') form.elements.apPassword.value = '';
        setTimeout(refreshAp, 1800);
      } catch {
        $('apStatus').textContent = 'Connection interrupted. Reconnect and check the access point status.';
      } finally {
        $(id).disabled = false;
      }
    });
  }
  refreshAp();
  for (const [id, route] of [['saveBackup', '/admin/backup'], ['restoreBackup', '/admin/backup/restore']]) {
    $(id).addEventListener('click', async () => {
      const restore = id === 'restoreBackup';
      if (restore && !confirm('Restore the saved configuration and restart Watchtower?')) return;
      $('saveBackup').disabled = $('restoreBackup').disabled = true;
      let restarting = false;
      try {
        const body = new URLSearchParams({csrf: $('settingsForm').elements.csrf.value});
        const response = await adminFetch(route, {method: 'POST', body});
        $('backupStatus').textContent = await response.text();
        restarting = restore && response.ok;
      } catch {
        $('backupStatus').textContent = 'Could not reach Watchtower.';
      } finally {
        $('saveBackup').disabled = $('restoreBackup').disabled = restarting;
      }
    });
  }
  $('ledForm').addEventListener('submit', async event => {
    event.preventDefault();
    const form = event.currentTarget;
    const button = form.querySelector('button');
    button.disabled = true;
    try {
      const body = new URLSearchParams(new FormData(form));
      body.set('csrf', $('settingsForm').elements.csrf.value);
      const response = await adminFetch('/admin/leds', {method: 'POST', body});
      const message = await response.text();
      if (!response.ok) throw Error(message);
      $('ledFeedback').textContent = message;
      await syncStealth();
    } catch (error) {
      $('ledFeedback').textContent = error.message || 'Could not reach Watchtower.';
    } finally {
      button.disabled = false;
    }
  });
  const nav = document.querySelector('.settings-nav');
  for (const link of [...nav.querySelectorAll('a')].sort((a, b) => a.textContent.localeCompare(b.textContent)))
    nav.append(link);
  async function adminAction(route, fields = {}) {
    const body = new URLSearchParams({csrf: $('settingsForm').elements.csrf.value, ...fields});
    const response = await adminFetch(route, {method: 'POST', body});
    const text = await response.text();
    if (!response.ok) throw Error(text);
    return text;
  }
  $('logoutAdmin').addEventListener('click', async () => {
    const button = $('logoutAdmin');
    button.disabled = true;
    try {
      await adminAction('/admin/logout');
      location.replace('/');
    } catch (error) { $('rebootFeedback').textContent = error.message; button.disabled = false; }
  });
  $('rebootSystem').addEventListener('click', async () => {
    if (!confirm('Reboot Watchtower now?')) return;
    try { $('rebootFeedback').textContent = await adminAction('/admin/reboot'); }
    catch (error) { $('rebootFeedback').textContent = error.message; }
  });
  function showStealth(enabled) {
    const button = $('stealthMode');
    button.dataset.enabled = String(enabled);
    button.setAttribute('aria-pressed', String(!enabled));
    button.textContent = enabled ? 'Turn Stealth Mode On' : 'Turn Stealth Mode Off';
    $('ledForm').elements.namedItem('enabled').checked = enabled;
  }
  async function syncStealth() {
    const response = await adminFetch('/admin/leds', {cache: 'no-store'});
    if (!response.ok) throw Error('Could not read the current LED state.');
    const state = await response.json();
    showStealth(state.enabled);
  }
  $('stealthMode').addEventListener('click', async () => {
    const button = $('stealthMode');
    button.disabled = true;
    try {
      const body = new URLSearchParams({csrf: $('settingsForm').elements.csrf.value, action: 'toggle'});
      const response = await adminFetch('/admin/stealth', {method: 'POST', body});
      if (!response.ok) throw Error(await response.text());
      const state = await response.json();
      showStealth(state.enabled);
      $('stealthFeedback').textContent = state.enabled ? 'Stealth Mode off. Configured RGB patterns are enabled.' : 'Stealth Mode on. RGB indicator is off.';
    } catch (error) { $('stealthFeedback').textContent = error.message; }
    finally { button.disabled = false; }
  });
  syncStealth().catch(error => { $('stealthFeedback').textContent = error.message; $('stealthMode').textContent = 'Toggle Stealth Mode'; });
  function capacity(bytes) {
    if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / 1048576).toFixed(2) + ' MB';
  }
  function healthCard(title, percent, details) {
    const card = document.createElement('article');
    card.className = 'health-card';
    const label = document.createElement('h3');
    label.textContent = title;
    const value = document.createElement('div');
    value.className = 'health-value';
    const known = Number.isFinite(percent) && percent >= 0;
    const usage = known ? Math.min(100, percent) : 0;
    value.textContent = known ? usage.toFixed(1) + '%' : title === 'CPU' ? 'Sampling…' : 'Unavailable';
    const bar = document.createElement('progress');
    bar.max = 100;
    bar.value = usage;
    bar.setAttribute('aria-label', title + ' usage');
    bar.hidden = !known;
    const info = document.createElement('p');
    info.textContent = details;
    card.append(label, value, bar, info);
    return card;
  }
  let healthLoading = false;
  async function health() {
    if (healthLoading) return;
    healthLoading = true;
    try {
      const response = await adminFetch('/admin/health', {cache: 'no-store'});
      if (!response.ok) throw Error('Health information unavailable');
      const h = await response.json();
      const memory = 100 * (1 - h.heapFree / h.heapTotal);
      const flash = 100 * h.firmwareUsed / h.flashTotal;
      const storageFree = h.storageTotal * (1 - h.storageUsedPercent / 100);
      const cpu = healthCard('CPU', h.cpu, h.clockMHz + ' MHz clock');
      const ram = healthCard('Memory', memory,
        capacity(h.heapTotal - h.heapFree) + ' used · ' + capacity(h.heapFree) + ' available');
      if (Number.isFinite(h.heapMinimum) && Number.isFinite(h.heapLargest)) {
        const diagnostic = document.createElement('small');
        diagnostic.textContent = 'Lowest free since restart: ' + capacity(h.heapMinimum) + ' · Largest free block: ' + capacity(h.heapLargest);
        ram.append(diagnostic);
      }
      const firmware = healthCard('Firmware flash', flash,
        capacity(h.firmwareUsed) + ' firmware · ' + capacity(h.flashTotal) + ' total flash');
      const note = document.createElement('small');
      note.textContent = capacity(h.flashTotal - h.firmwareUsed) +
        ' outside firmware, including reserved partitions';
      firmware.append(note);
      const storage = healthCard('Configuration storage', h.storageUsedPercent,
        h.storageUsedPercent < 0 ? 'Storage information unavailable' :
          capacity(storageFree) + ' available · ' + capacity(h.storageTotal) + ' total');
      const explanation = document.createElement('small');
      explanation.textContent = 'Usage based on allocated configuration entries';
      storage.append(explanation);
      $('healthStatus').replaceChildren(cpu, ram, firmware, storage);
      $('healthError').hidden = true;
    } catch (error) {
      $('healthError').textContent = error.message;
      $('healthError').hidden = false;
    } finally {
      healthLoading = false;
    }
  }
  health();
  setInterval(health, 5000);
  $('settingsForm').addEventListener('submit', () => {
    $('saveButton').disabled = true;
    $('saveButton').textContent = 'Saving Settings…'
  });
})();
