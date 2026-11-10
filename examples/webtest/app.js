document.getElementById('status').textContent = 'Local files loaded; JavaScript shares the UI document.';
const context = document.getElementById('bitmap').getContext('2d');
const gradient = context.createLinearGradient(0, 0, 48, 24);
gradient.addColorStop(0, '#2468aa');
gradient.addColorStop(1, '#24aa78');
context.fillStyle = gradient;
context.fillRect(0, 0, 48, 24);
context.fillStyle = 'white';
context.fillRect(8, 8, 12, 8);
