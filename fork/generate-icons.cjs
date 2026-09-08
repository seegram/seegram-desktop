// Regenerate committed PNG/ICO assets from artwork/app-icon.svg. Requires sharp.
const fs = require('fs');
const path = require('path');
const sharp = require('sharp');
const root = path.resolve(__dirname, '..');
const svg = fs.readFileSync(path.join(root,'fork/artwork/app-icon.svg'));
async function render(file,size) { await sharp(svg,{density:300}).resize(size,size).png().toFile(path.join(root,file)); }
(async()=>{
for(const size of [16,32,48,64,128,256,512]) for(const scale of [1,2]) {
 const name = `icon${size}${scale===2?'@2x':''}.png`;
 await render(`Telegram/Resources/art/${name}`,size*scale);
}
for(const size of [16,32,128,256,512]) for(const scale of [1,2]) {
 const suffix=scale===2?'@2x':'';
 await render(`Telegram/Telegram/Images.xcassets/Icon.appiconset/icon${size}${suffix}.png`,size*scale);
 await render(`Telegram/Telegram/Images.xcassets/Icon.iconset/icon_${size}x${size}${suffix}.png`,size*scale);
}
for(const name of ['logo_256.png','logo_256_no_margin.png']) await render(`Telegram/Resources/art/${name}`,256);
await render('Telegram/Resources/art/icon_round512@2x.png',1024);
// Windows ICO directory containing PNG entries, decoded natively by Windows.
async function writeIco(input, filename) {
 const entries=[];
 for(const size of [16,24,32,48,64,128,256]) {
  entries.push({size,png:await sharp(input,{density:300}).resize(size,size).png().toBuffer()});
 }
 const header=Buffer.alloc(6+16*entries.length); header.writeUInt16LE(1,2);header.writeUInt16LE(entries.length,4);
 let offset=header.length;
 entries.forEach(({size,png},i)=>{const at=6+i*16;header[at]=header[at+1]=size===256?0:size;header.writeUInt16LE(1,at+4);header.writeUInt16LE(32,at+6);header.writeUInt32LE(png.length,at+8);header.writeUInt32LE(offset,at+12);offset+=png.length;});
 fs.writeFileSync(path.join(root,filename),Buffer.concat([header,...entries.map(e=>e.png)]));
}
await writeIco(svg,'Telegram/Resources/art/icon256.ico');
await writeIco(path.join(root,'Telegram/Resources/art/disguise/telegram.png'),'Telegram/Resources/art/disguise/telegram.ico');
})();
