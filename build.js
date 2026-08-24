const fs = require('fs');
const path = require('path');

const dest = path.join(__dirname, 'www');
if (!fs.existsSync(dest)) {
    fs.mkdirSync(dest);
}

fs.copyFileSync(path.join(__dirname, 'index.html'), path.join(dest, 'index.html'));
fs.copyFileSync(path.join(__dirname, 'app.js'), path.join(dest, 'app.js'));
console.log('Build completed! Files copied to www/');

const androidDest = path.join(__dirname, 'android', 'app', 'src', 'main', 'assets', 'public');
if (fs.existsSync(androidDest)) {
    fs.copyFileSync(path.join(__dirname, 'index.html'), path.join(androidDest, 'index.html'));
    fs.copyFileSync(path.join(__dirname, 'app.js'), path.join(androidDest, 'app.js'));
    console.log('Files copied to android assets/public/');
}
