document.addEventListener('DOMContentLoaded', () => {
    // DOM Elements
    const geminiKeyInput = document.getElementById('geminiKey');
    const spreadsheetIdInput = document.getElementById('spreadsheetId');
    const serviceAccountInput = document.getElementById('serviceAccountJson');
    const settingsContainer = document.getElementById('settingsContainer');
    const toggleSettingsBtn = document.getElementById('toggleSettings');
    const saveSettingsBtn = document.getElementById('saveSettings');
    const clearSettingsBtn = document.getElementById('clearSettings');
    
    const imageInput = document.getElementById('imageInput');
    const dropZone = document.getElementById('dropZone');
    const imagePreview = document.getElementById('imagePreview');
    const fileNameDisplay = document.getElementById('fileNameDisplay');
    const imagePathInput = document.getElementById('imagePathInput');
    
    const invoiceIdsInput = document.getElementById('invoiceIds');
    const processBtn = document.getElementById('processBtn');
    const fetchBtn = document.getElementById('fetchBtn');
    const searchInput = document.getElementById('searchInput');
    const logContainer = document.getElementById('log');
    
    const tableHeader = document.getElementById('tableHeader');
    const tableBody = document.getElementById('tableBody');

    let currentSheetData = null; // Store fetched data for filtering
    let selectedFile = null;     // Store selected/dropped/pasted image

    // Constants
    const PROMPT = `
    Analyze this image of a shipping spreadsheet.
    Extract all data rows. For each row, extract the following 9 columns exactly in this order:
    1. TBL NO (This is the BILL)
    2. SHIPPER (The invoice ID)
    3. CONTAINER NO.
    4. TYPE
    5. SEAL NO.
    6. TRUCK NO. (If the content here is just a truck size, replace it with the actual truck plate no. Look carefully).
    7. DRIVER NAME
    8. CNEE
    9. DATE

    Return ONLY a CSV format with one row per line. Do not include headers, labels, or any other text.
    Use a comma as the separator. If a value contains a comma, omit it or replace with a space.
    `;

    // Initialize Settings from LocalStorage
    function loadSettings() {
        geminiKeyInput.value = localStorage.getItem('gemini_api_key') || '';
        spreadsheetIdInput.value = localStorage.getItem('spreadsheet_id') || '';
        serviceAccountInput.value = localStorage.getItem('service_account_json') || '';
    }

    loadSettings();

    // Event Listeners: Image Drag/Drop/Paste
    dropZone.addEventListener('click', () => imageInput.click());
    
    dropZone.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropZone.style.borderColor = '#4facfe';
    });
    
    dropZone.addEventListener('dragleave', (e) => {
        e.preventDefault();
        dropZone.style.borderColor = '';
    });
    
    dropZone.addEventListener('drop', (e) => {
        e.preventDefault();
        dropZone.style.borderColor = '';
        if (e.dataTransfer.files && e.dataTransfer.files.length > 0) {
            handleImageFile(e.dataTransfer.files[0]);
        }
    });

    imageInput.addEventListener('change', (e) => {
        if (e.target.files && e.target.files.length > 0) {
            handleImageFile(e.target.files[0]);
        }
    });

    document.addEventListener('paste', (e) => {
        if (!e.clipboardData) return;

        // Try items first (better for apps like WeChat, Snipping Tool, etc.)
        const items = e.clipboardData.items;
        for (let i = 0; i < items.length; i++) {
            if (items[i].type.indexOf('image') !== -1) {
                const file = items[i].getAsFile();
                if (file) {
                    handleImageFile(file);
                    e.preventDefault();
                    return;
                }
            }
        }

        // Fallback to files
        if (e.clipboardData.files && e.clipboardData.files.length > 0) {
            handleImageFile(e.clipboardData.files[0]);
            e.preventDefault();
        }
    });

    imagePathInput.addEventListener('input', () => {
        if (imagePathInput.value.trim()) {
            selectedFile = null; // Prioritize path over file
            imagePreview.classList.add('d-none');
            fileNameDisplay.textContent = "Using Web URL";
            fileNameDisplay.classList.remove('d-none');
        }
    });

    function handleImageFile(file) {
        if (!file.type.startsWith('image/')) {
            alert("Please provide an image file.");
            return;
        }
        selectedFile = file;
        imagePathInput.value = ''; // clear input
        
        const reader = new FileReader();
        reader.onload = (e) => {
            imagePreview.src = e.target.result;
            imagePreview.classList.remove('d-none');
            fileNameDisplay.textContent = file.name || "Pasted Image";
            fileNameDisplay.classList.remove('d-none');
        };
        reader.readAsDataURL(file);
    }

    // Settings Event Listeners
    toggleSettingsBtn.addEventListener('click', () => {
        settingsContainer.classList.toggle('hidden');
    });

    searchInput.addEventListener('input', () => {
        if (currentSheetData) {
            renderTable(currentSheetData);
        }
    });

    saveSettingsBtn.addEventListener('click', () => {
        localStorage.setItem('gemini_api_key', geminiKeyInput.value);
        localStorage.setItem('spreadsheet_id', spreadsheetIdInput.value);
        localStorage.setItem('service_account_json', serviceAccountInput.value);
        log("Settings saved to browser cache.");
    });

    clearSettingsBtn.addEventListener('click', () => {
        localStorage.clear();
        loadSettings();
        log("Browser cache cleared.");
    });

    processBtn.addEventListener('click', async () => {
        const invoiceIds = invoiceIdsInput.value.trim().split('\n').filter(id => id.trim());
        const geminiKey = geminiKeyInput.value;
        const spreadsheetId = spreadsheetIdInput.value;
        const saJson = serviceAccountInput.value;
        const urlInput = imagePathInput.value.trim();

        if ((!selectedFile && !urlInput) || !invoiceIds.length || !geminiKey || !spreadsheetId || !saJson) {
            alert("Please complete all fields (Image, IDs, and Settings).");
            return;
        }

        try {
            processBtn.disabled = true;
            processBtn.innerHTML = '<i class="fa-solid fa-spinner fa-spin me-2"></i>Processing...';
            
            let base64Image = null;
            if (selectedFile) {
                log("Reading local image file...");
                base64Image = await fileToBase64(selectedFile);
            } else {
                log(`Fetching image from URL: ${urlInput}`);
                try {
                    const resp = await fetch(urlInput);
                    if (!resp.ok) throw new Error("Failed to fetch image from URL");
                    const blob = await resp.blob();
                    base64Image = await fileToBase64(blob);
                } catch (e) {
                    throw new Error("Could not load image from URL. Browsers block local C:\\ paths. Please drag & drop or use Ctrl+V to paste the image instead.");
                }
            }
            
            log("Sending to Gemini...");
            const extractedRows = await callGemini(geminiKey, base64Image, invoiceIds);
            log(`Extracted ${extractedRows.length} rows.`);

            log("Authenticating with Google Sheets...");
            const accessToken = await getGoogleAccessToken(JSON.parse(saJson));
            
            log("Pushing to Google Sheets...");
            await appendToGoogleSheets(accessToken, spreadsheetId, extractedRows);
            log("Successfully pushed to Google Sheets.");

            log("Fetching updated sheet data...");
            await fetchAndRenderSheet(accessToken, spreadsheetId);

        } catch (err) {
            log(`ERROR: ${err.message}`);
            console.error(err);
        } finally {
            processBtn.disabled = false;
            processBtn.innerHTML = '<i class="fa-solid fa-bolt me-2"></i>Step 3: AI Extract & Sync to Sheets';
        }
    });

    fetchBtn.addEventListener('click', async () => {
        const spreadsheetId = spreadsheetIdInput.value;
        const saJson = serviceAccountInput.value;
        if (!spreadsheetId || !saJson) {
            alert("Please provide Spreadsheet ID and Service Account JSON.");
            return;
        }

        try {
            fetchBtn.disabled = true;
            log("Authenticating...");
            const accessToken = await getGoogleAccessToken(JSON.parse(saJson));
            log("Fetching...");
            await fetchAndRenderSheet(accessToken, spreadsheetId);
        } catch (err) {
            log(`ERROR: ${err.message}`);
        } finally {
            fetchBtn.disabled = false;
        }
    });

    // Helper Functions
    function log(message) {
        const div = document.createElement('div');
        div.textContent = `> ${new Date().toLocaleTimeString()}: ${message}`;
        logContainer.appendChild(div);
        logContainer.scrollTop = logContainer.scrollHeight;
    }

    function fileToBase64(file) {
        return new Promise((resolve, reject) => {
            const reader = new FileReader();
            reader.readAsDataURL(file);
            reader.onload = () => resolve(reader.result.split(',')[1]);
            reader.onerror = error => reject(error);
        });
    }

    async function callGemini(apiKey, base64Image, clientIds) {
        const url = `https://generativelanguage.googleapis.com/v1beta/models/gemini-3.1-flash-lite-preview:generateContent?key=${apiKey}`;
        const body = {
            contents: [{
                parts: [
                    { text: PROMPT },
                    { inline_data: { mime_type: "image/jpeg", data: base64Image } }
                ]
            }]
        };

        const resp = await fetch(url, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body)
        });

        if (!resp.ok) {
            const error = await resp.json();
            throw new Error(`Gemini API error: ${error.error.message}`);
        }

        const data = await resp.json();
        const text = data.candidates[0].content.parts[0].text;
        
        const lines = text.trim().split('\n');
        const rows = lines.map((line, index) => {
            const cols = line.split(',').map(c => c.trim());
            // Overwrite invoice_no (column 2) with provided ID if available
            if (index < clientIds.length) {
                cols[1] = clientIds[index];
            }
            return cols;
        });

        return rows;
    }

    async function getGoogleAccessToken(sa) {
        const header = { alg: 'RS256', typ: 'JWT' };
        const now = Math.floor(Date.now() / 1000);
        const payload = {
            iss: sa.client_email,
            scope: 'https://www.googleapis.com/auth/spreadsheets',
            aud: 'https://oauth2.googleapis.com/token',
            exp: now + 3600,
            iat: now
        };

        const sHeader = JSON.stringify(header);
        const sPayload = JSON.stringify(payload);
        const sJWT = KJUR.jws.JWS.sign("RS256", sHeader, sPayload, sa.private_key);

        const resp = await fetch('https://oauth2.googleapis.com/token', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer&assertion=${sJWT}`
        });

        if (!resp.ok) {
            const error = await resp.json();
            throw new Error(`Google Auth error: ${error.error_description || error.error}`);
        }

        const data = await resp.json();
        return data.access_token;
    }

    async function appendToGoogleSheets(token, spreadsheetId, rows) {
        const url = `https://sheets.googleapis.com/v4/spreadsheets/${spreadsheetId}/values/CONTAINER!A1:append?valueInputOption=USER_ENTERED`;
        const body = { values: rows };

        const resp = await fetch(url, {
            method: 'POST',
            headers: {
                'Authorization': `Bearer ${token}`,
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(body)
        });

        if (!resp.ok) {
            const error = await resp.json();
            throw new Error(`Google Sheets Append error: ${error.error.message}`);
        }
    }

    async function fetchAndRenderSheet(token, spreadsheetId) {
        // Fetch values and background colors from 2026 sheet
        const range = "2026!A1:Z500";
        const url = `https://sheets.googleapis.com/v4/spreadsheets/${spreadsheetId}?ranges=${range}&fields=sheets(data(rowData(values(effectiveValue,effectiveFormat(backgroundColor)))))`;

        const resp = await fetch(url, {
            headers: { 'Authorization': `Bearer ${token}` }
        });

        if (!resp.ok) {
            const error = await resp.json();
            throw new Error(`Google Sheets Fetch error: ${error.error.message}`);
        }

        const data = await resp.json();
        const sheet = data.sheets[0];
        currentSheetData = sheet.data[0].rowData; // Cache globally for filtering

        renderTable(currentSheetData);
    }

    function renderTable(rowData) {
        tableHeader.innerHTML = '';
        tableBody.innerHTML = '';

        if (!rowData || rowData.length === 0) return;

        const searchTerm = searchInput.value.toLowerCase().trim();

        // Get header indices
        const headers = rowData[0].values.map(v => (v.effectiveValue?.stringValue || '').toUpperCase());
        const targetCols = [
            "CLIENT", "INV NO", "REF NO", "TYPE", "INV DAT", 
            "EXPRESS CO", "CONTAINER", "BILL"
        ];
        
        const colMap = targetCols.map(tc => ({
            name: tc,
            index: headers.findIndex(h => h.includes(tc))
        })).filter(c => c.index !== -1);

        // Display rows in descending order (reverse), but keep header at top
        const headerRow = rowData[0];
        const dataRows = rowData.slice(1).reverse();
        
        // Apply search filter if active
        let filteredDataRows = dataRows.filter(row => {
            // Check if row is actually empty (no values or all values empty)
            if (!row.values || row.values.every(cell => {
                const eff = cell.effectiveValue;
                return !eff || (!eff.stringValue && eff.numberValue === undefined && eff.boolValue === undefined);
            })) return false;
            
            if (!searchTerm) return true;
            
            return row.values.some(cell => {
                const eff = cell.effectiveValue;
                let val = '';
                if (eff) {
                    if (eff.stringValue) val = eff.stringValue;
                    else if (eff.numberValue !== undefined) val = eff.numberValue.toString();
                    else if (eff.boolValue !== undefined) val = eff.boolValue.toString();
                }
                return val.toLowerCase().includes(searchTerm);
            });
        });

        const sortedRows = [headerRow, ...filteredDataRows];

        sortedRows.forEach((row, rowIndex) => {
            if (!row.values) return;
            const tr = document.createElement('tr');
            
            colMap.forEach(col => {
                const cell = row.values[col.index];
                if (!cell) return;
                
                const tag = rowIndex === 0 ? 'th' : 'td';
                const el = document.createElement(tag);
                
                // Get Value
                let val = '';
                const eff = cell.effectiveValue;
                if (eff) {
                    if (eff.stringValue) val = eff.stringValue;
                    else if (eff.numberValue !== undefined) {
                        // Date conversion for INV DAT (Google Sheets serial date: days since 1899-12-30)
                        if (col.name === "INV DAT" && rowIndex > 0) {
                            const date = new Date((eff.numberValue - 25569) * 86400 * 1000);
                            val = date.toLocaleDateString();
                        } else {
                            val = eff.numberValue;
                        }
                    }
                    else if (eff.boolValue !== undefined) val = eff.boolValue;
                }
                el.textContent = val;

                // Get Background Color
                const bg = cell.effectiveFormat?.backgroundColor;
                if (bg && rowIndex > 0) { // Only apply spreadsheet colors to data rows
                    const r = Math.round((bg.red || 0) * 255);
                    const g = Math.round((bg.green || 0) * 255);
                    const b = Math.round((bg.blue || 0) * 255);
                    el.style.backgroundColor = `rgb(${r},${g},${b})`;
                    // Basic contrast check
                    const brightness = (r * 299 + g * 587 + b * 114) / 1000;
                    if (brightness < 128) el.style.color = 'white';
                }

                if (rowIndex === 0) {
                    tableHeader.appendChild(el);
                } else {
                    tr.appendChild(el);
                }
            });
            if (rowIndex > 0) tableBody.appendChild(tr);
        });
    }
});
