(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const paths = {
    wifi: '<path d="M3 8a15 15 0 0 1 18 0M6 12a10 10 0 0 1 12 0M9 16a5 5 0 0 1 6 0"/><circle cx="12" cy="20" r="1"/>',
    ethernet: '<path d="M5 3h14v12h-4v4H9v-4H5zM8 3v5m4-5v5m4-5v5m-4 11v3"/>',
    unknown: '<rect x="8" y="3" width="8" height="6" rx="1"/><path d="M12 9v6M4 15h16M4 15v5m8-5v5m8-5v5"/>',
    unused: '<path d="M5 4h14v13h-4v3H9v-3H5zM8 4v5m4-5v5m4-5v5"/>'
  };
  let selectedSubnet = '';
  let appearance = null;
  function parts(ip) {
    if (!/^\d+\.\d+\.\d+\.\d+$/.test(ip || '')) return null;
    const values = ip.split('.').map(Number);
    return values.every(v => v >= 0 && v <= 255) && values[0] > 0 && values[0] < 224 && values[0] !== 127 ? values : null;
  }
  function prefix(ip) {
    const values = parts(ip);
    return values ? values.slice(0, 3).join('.') + '.' : '';
  }
  function connectionType(device) {
    const type = (device.connection || '').toLowerCase();
    return /wi.?fi|wireless|wlan|2[.]?4g|5g|6g|mlo/.test(type) ? 'wifi'
      : /ethernet|wired|lan|eth/.test(type) ? 'ethernet' : 'unknown';
  }
  function state(device) {
    return device.status !== 'ONLINE' ? 'offline' : device.policy === 'Authorized' ? 'online'
      : device.policy === 'Acknowledged' ? 'ack' : 'alert';
  }
  function icon(type) {
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('viewBox', '0 0 24 24');
    svg.setAttribute('aria-hidden', 'true');
    svg.innerHTML = paths[type];
    return svg;
  }
  function render(report, onSelect) {
    const grid = $('deviceHeatmap');
    const focused = grid.contains(document.activeElement) ? document.activeElement.dataset.port : null;
    grid.tabIndex = 0;
    grid.setAttribute('aria-label', '256 device ports numbered 000 to 255 in 32 columns and 8 rows.');
    const subnets = [...new Set([prefix(report.ethernetIP), ...report.devices.map(d => prefix(d.ip))].filter(Boolean))].sort();
    if (!subnets.includes(selectedSubnet)) selectedSubnet = prefix(report.ethernetIP) || subnets[0] || '';
    let subnetLabel = $('heatmapSubnetLabel');
    if (!subnetLabel) {
      subnetLabel = document.createElement('label');
      subnetLabel.id = 'heatmapSubnetLabel';
      subnetLabel.className = 'switch-subnet';
      subnetLabel.textContent = 'IP range ';
      const select = document.createElement('select');
      select.id = 'heatmapSubnet';
      subnetLabel.append(select);
      grid.parentElement.querySelector('.switch-caption').append(subnetLabel);
    }
    subnetLabel.hidden = subnets.length < 2;
    const select = $('heatmapSubnet');
    select.replaceChildren();
    for (const subnet of subnets) {
      const option = document.createElement('option');
      option.value = subnet;
      option.textContent = subnet + 'x';
      option.selected = subnet === selectedSubnet;
      select.append(option);
    }
    select.onchange = () => { selectedSubnet = select.value; render(report, onSelect); };
    const ports = Array.from({length: 256}, () => []);
    for (const device of report.devices) {
      const address = parts(device.ip);
      if (address && prefix(device.ip) === selectedSubnet) ports[address[3]].push(device);
    }
    const assigned = ports.filter(devices => devices.length).length;
    $('heatmapCount').textContent = (selectedSubnet ? selectedSubnet + 'x · ' : '') + assigned + ' assigned · ' + (256 - assigned) + ' unused';
    if (!assigned) $('heatmapDetail').textContent = report.completed ? 'No devices in this IP range.' : 'Waiting for the first scan.';
    grid.replaceChildren();
    for (let index = 0; index < 256; ++index) {
      const devices = ports[index];
      // Prefer an online record when an address was reused by another device.
      devices.sort((a, b) => Number(b.status === 'ONLINE') - Number(a.status === 'ONLINE') ||
        ({alert: 0, ack: 1, online: 2, offline: 3}[state(a)] - {alert: 0, ack: 1, online: 2, offline: 3}[state(b)]));
      const device = devices[0];
      const portNumber = index;
      const tile = document.createElement('button');
      tile.type = 'button';
      tile.dataset.port = String(portNumber);
      const number = document.createElement('span');
      number.className = 'heat-port-number';
      number.textContent = String(portNumber).padStart(3, '0');
      tile.append(number);
      if (!device) {
        tile.className = 'heat-tile heat-unused';
        tile.disabled = true;
        tile.title = 'Port ' + String(portNumber).padStart(3, '0') + (selectedSubnet ? ' · ' + selectedSubnet + portNumber : '') + ' · Unused';
        tile.append(icon('unused'));
      } else {
        const type = connectionType(device);
        tile.dataset.connection = type;
        tile.dataset.mac = device.mac || device.ip;
        tile.dataset.ip = device.ip;
        tile.className = 'heat-tile heat-' + state(device);
        const label = ['Port ' + String(portNumber).padStart(3, '0'), device.name || device.hostname || 'Unknown device', device.ip,
          device.mac || 'MAC unavailable', device.status === 'ONLINE' ? 'Online' : 'Offline', device.policy,
          device.connection || 'Connection not reported'].join(' · ') + (devices.length > 1 ? ' · ' + devices.length + ' records at this address' : '');
        tile.title = label;
        tile.append(icon(type));
        tile.addEventListener('focus', () => { $('heatmapDetail').textContent = label; });
        tile.addEventListener('mouseenter', () => { $('heatmapDetail').textContent = label; });
        tile.addEventListener('click', () => onSelect(devices.length > 1 ? {...device, mac: ''} : device));
      }
      tile.setAttribute('aria-label', tile.title);
      grid.append(tile);
      if (focused === tile.dataset.port && !tile.disabled) tile.focus({preventScroll: true});
    }
    staggerPorts();
  }
  function configure(settings) {
    if (!settings || !Array.isArray(settings.colors) || !Array.isArray(settings.blink) || !Array.isArray(settings.speed)) return;
    const states = ['online', 'alert', 'ack', 'offline', 'unused'];
    if (states.some((_, i) => !/^#[0-9a-f]{6}$/i.test(settings.colors[i]) ||
        typeof settings.blink[i] !== 'boolean' || !Number.isInteger(settings.speed[i]) || settings.speed[i] < 2 || settings.speed[i] > 25)) return;
    appearance = {colors:[...settings.colors], blink:[...settings.blink], speed:[...settings.speed], randomOnline:settings.randomOnline === true};
    for (const [i, state] of states.entries()) {
      const color = settings.colors[i];
      const rgb = [1, 3, 5].map(start => parseInt(color.slice(start, start + 2), 16));
      const linear = rgb.map(value => { const c = value / 255; return c <= .04045 ? c / 12.92 : Math.pow((c + .055) / 1.055, 2.4); });
      const luminance = linear[0] * .2126 + linear[1] * .7152 + linear[2] * .0722;
      const style = document.documentElement.style;
      style.setProperty('--heat-' + state + '-color', color);
      style.setProperty('--heat-' + state + '-ink', (luminance + .05) / .05 > 1.05 / (luminance + .05) ? '#000000' : '#ffffff');
      style.setProperty('--heat-' + state + '-blink', settings.blink[i] ? 'port-blink' : 'none');
      style.setProperty('--heat-' + state + '-duration', (10 / settings.speed[i]) + 's');
    }
    staggerPorts();
  }
  function staggerPorts() {
    if (!appearance) return;
    for (const tile of document.querySelectorAll('#deviceHeatmap .heat-tile')) {
      const index = tile.classList.contains('heat-online') ? 0 : tile.classList.contains('heat-alert') ? 1 : tile.classList.contains('heat-ack') ? 2 : -1;
      if (!appearance.randomOnline || index < 0 || !appearance.blink[index]) { tile.style.removeProperty('animation-delay'); continue; }
      let hash = 2166136261;
      for (const char of (tile.dataset.ip || '') + (tile.dataset.mac || '')) hash = Math.imul(hash ^ char.charCodeAt(0), 16777619) >>> 0;
      const period = 10 / appearance.speed[index];
      const phase = (hash % 10000) / 10000;
      tile.style.animationDelay = -((performance.now() / 1000 + phase * period) % period) + 's';
    }
  }
  async function refreshSettings() {
    try {
      const response = await fetch('/api/heatmap', {cache: 'no-store'});
      if (response.ok) configure(await response.json());
    } catch (_) { /* Keep the last confirmed theme while the system is unreachable. */ }
  }
  configure({colors:['#2d9b65','#d34354','#e7b844','#8592a5','#28313e'], blink:[false,true,true,false,false], speed:[10,20,4,10,10]});
  refreshSettings();
  setInterval(refreshSettings, 30000);
  window.watchtowerHeatmap = {render, configure};
})();
