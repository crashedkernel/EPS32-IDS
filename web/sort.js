const deviceSort = (() => {
  const collator = new Intl.Collator(undefined, {numeric: true, sensitivity: 'base'});
  const ip = value => (value || '').split('.').reduce((result, octet) => result * 256 + Number(octet), 0);
  function value(device, key) {
    if (key === 'ip') return ip(device.ip);
    if (key === 'actions') return Number(device.policyAvailable);
    if (key === 'presence') return device.status;
    if (key === 'status') return device.status + ' ' + device.policy;
    if (key === 'connection') return device.connection || 'Not reported';
    return device[key] ?? '';
  }
  function compare(a, b, key, ascending) {
    const left = value(a, key), right = value(b, key);
    const order = typeof left === 'number' && typeof right === 'number'
      ? left - right : collator.compare(left, right);
    return (order || ip(a.ip) - ip(b.ip) || collator.compare(a.mac, b.mac)) * (ascending ? 1 : -1);
  }
  function mark(button, key, ascending) {
    const active = button.dataset.key === key;
    button.parentElement.setAttribute('aria-sort', active ? (ascending ? 'ascending' : 'descending') : 'none');
    button.querySelector('.sort-marker').textContent = active ? (ascending ? '▲' : '▼') : '';
  }
  return {compare, mark};
})();
