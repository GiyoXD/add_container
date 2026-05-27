const fs = require('fs');
const path = require('path');

const dest = path.join(__dirname, 'www');
if (!fs.existsSync(dest)) {
    fs.mkdirSync(dest);
}

fs.copyFileSync(path.join(__dirname, 'index.html'), path.join(dest, 'index.html'));
fs.copyFileSync(path.join(__dirname, 'app.js'), path.join(dest, 'app.js'));
console.log('Build completed! Files copied to www/');
