(() => {
  'use strict';
  const $ = id => document.getElementById(id);

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
  $('source').addEventListener('change', () => {
    const gl = $('source').value === 'glinet';
    $('transport').value = gl ? 'ssh' : 'http';
    document.querySelector('[name=routerPort]').value = gl ? 22 : 80
  });
  function ethernetFields() {
    const enabled = $('ipMode').value === 'static';
    $('staticFields').hidden = !enabled;
    for (const field of $('staticFields').querySelectorAll('input')) field.required = enabled;
  }
  $('ipMode').addEventListener('change', ethernetFields);
  ethernetFields();
  $('mode').addEventListener('change', addressFields);
  addressFields();
  $('transport').addEventListener('change', function() {
    document.querySelector('[name=routerPort]').value = this.value === 'ssh' ? 22 : 80
  });
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
      const response = await fetch('/admin/ap', {cache: 'no-store'});
      if (!response.ok) return;
      const state = await response.json();
      if (state.pending) return;
      $('apStatus').textContent = state.running
        ? 'Running on channel ' + state.channel + ' (' + (2407 + 5 * state.channel) + ' MHz)' + (state.widthMHz ? ' · ' + state.widthMHz + ' MHz width' : '')
        : 'Stopped';
      $('apStatus').textContent += state.enabledAtBoot ? ' · Enabled at startup' : ' · Disabled at startup';
      $('apPersistent').dataset.enabled = String(state.enabledAtBoot);
      $('apPersistent').textContent = state.enabledAtBoot ? 'Disable AP persistently' : 'Enable AP persistently';
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
        const response = await fetch('/admin/ap', {method: 'POST', body});
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
        const response = await fetch(route, {method: 'POST', body});
        $('backupStatus').textContent = await response.text();
        restarting = restore && response.ok;
      } catch {
        $('backupStatus').textContent = 'Could not reach Watchtower.';
      } finally {
        $('saveBackup').disabled = $('restoreBackup').disabled = restarting;
      }
    });
  }
  $('settingsForm').addEventListener('submit', () => {
    $('saveButton').disabled = true;
    $('saveButton').textContent = 'Saving settings...'
  });
})();
