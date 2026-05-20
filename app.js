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
    Extract all data rows. For each row, extract the following 10 columns exactly in this order:
    1. TBL NO (This is the BILL)
    2. SHIPPER (The invoice ID)
    3. CONTAINER NO.
    4. TYPE
    5. SEAL NO.
    6. TRUCK NO. (If the content here is just a truck size, replace it with the actual truck plate no. Look carefully).
    7. DRIVER NAME
    8. CNEE
    9. DATE
    10. PALLET: GROSS (The pallet gross weight, often labeled "p: gross" or "pallet gross" or similar. Extract the raw weight value, e.g. "1234.56" or "1234")

    Return ONLY a CSV format with one row per line. Do not include headers, labels, or any other text.
    Use a comma as the separator. If a value contains a comma, omit it or replace with a space.
    `;

    // Initialize Settings from LocalStorage
    function loadSettings() {
        geminiKeyInput.value = localStorage.getItem('gemini_api_key') || '';
        spreadsheetIdInput.value = localStorage.getItem('spreadsheet_id') || '';
        serviceAccountInput.value = localStorage.getItem('service_account_json') || '';

        // Load cached sheet data
        const cachedData = localStorage.getItem('cached_sheet_data');
        if (cachedData) {
            try {
                currentSheetData = JSON.parse(cachedData);
                renderTable(currentSheetData);
                log("Loaded cached sheet data.");
            } catch (e) {
                console.error("Failed to parse cached sheet data", e);
            }
        }
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

    // Toggle Web URL Input
    const toggleUrlInput = document.getElementById('toggleUrlInput');
    const urlInputContainer = document.getElementById('urlInputContainer');
    if (toggleUrlInput && urlInputContainer) {
        toggleUrlInput.addEventListener('click', (e) => {
            e.preventDefault();
            urlInputContainer.classList.toggle('hidden');
        });
    }

    // Toggle Data Entry Collapse
    const toggleDataEntry = document.getElementById('toggleDataEntry');
    const dataEntryBody = document.getElementById('dataEntryBody');
    if (toggleDataEntry && dataEntryBody) {
        toggleDataEntry.addEventListener('click', () => {
            dataEntryBody.classList.toggle('hidden');
            if (dataEntryBody.classList.contains('hidden')) {
                toggleDataEntry.innerHTML = '<i class="fa-solid fa-plus me-1"></i>Expand';
            } else {
                toggleDataEntry.innerHTML = '<i class="fa-solid fa-minus me-1"></i>Collapse';
            }
        });
        
        // Auto-collapse on mobile devices on page load to keep screen space clean
        if (window.innerWidth < 768) {
            dataEntryBody.classList.add('hidden');
            toggleDataEntry.innerHTML = '<i class="fa-solid fa-plus me-1"></i>Expand';
        }
    }

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
        const invoiceIds = invoiceIdsInput.value.trim().split(/[,\n]+/).map(id => id.trim()).filter(id => id.length > 0);
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
            while (cols.length < 10) {
                cols.push('');
            }
            const finalCols = cols.slice(0, 10);
            // Overwrite invoice_no (column 2) with provided ID if available
            if (index < clientIds.length) {
                finalCols[1] = clientIds[index];
            }
            return finalCols;
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
        const formattedRows = rows.map(row => {
            const r = [...row];
            while (r.length < 10) {
                r.push('');
            }
            return [...r.slice(0, 9), '', '', r[9]];
        });
        const body = { values: formattedRows };

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
        
        // Save to LocalStorage for offline persistence
        localStorage.setItem('cached_sheet_data', JSON.stringify(currentSheetData));

        renderTable(currentSheetData);
    }

    function formatDateToDDMMYY(date) {
        if (!date || isNaN(date.getTime())) return '';
        const day = String(date.getDate()).padStart(2, '0');
        const month = String(date.getMonth() + 1).padStart(2, '0');
        const year = String(date.getFullYear()).slice(-2);
        return `${day}-${month}-${year}`;
    }

    async function commitCrossBorderDate(invoiceId, originalRowIndex, selectedDate, dateInput, commitBtn, actionTd) {
        const spreadsheetId = spreadsheetIdInput.value;
        const saJson = serviceAccountInput.value;
        if (!spreadsheetId || !saJson) {
            alert("Please provide Spreadsheet ID and Service Account JSON in the Settings panel.");
            return;
        }

        try {
            dateInput.disabled = true;
            commitBtn.disabled = true;
            commitBtn.innerHTML = '<i class="fa-solid fa-spinner fa-spin me-1"></i>Saving';
            log(`Marking invoice ${invoiceId} as cross-border for ${selectedDate}...`);

            // 1. Get Access Token
            const token = await getGoogleAccessToken(JSON.parse(saJson));

            // 2. Put date value into Column G (index 6, which corresponds to Column G)
            const range = `2026!G${originalRowIndex}`;
            const url = `https://sheets.googleapis.com/v4/spreadsheets/${spreadsheetId}/values/${range}?valueInputOption=USER_ENTERED`;
            
            // Format selected date from YYYY-MM-DD to DD-MM-YY
            const parts = selectedDate.split('-');
            const yearShort = parts[0].slice(-2);
            const formattedDate = `${parts[2]}-${parts[1]}-${yearShort}`;

            const body = {
                values: [[formattedDate]]
            };

            const resp = await fetch(url, {
                method: 'PUT',
                headers: {
                    'Authorization': `Bearer ${token}`,
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify(body)
            });

            if (!resp.ok) {
                const error = await resp.json();
                throw new Error(`Google Sheets Update error: ${error.error.message}`);
            }

            log(`Successfully committed date ${formattedDate} for Invoice ${invoiceId}.`);

            // 3. Update global currentSheetData cache and save to localStorage
            if (currentSheetData && currentSheetData[originalRowIndex - 1]) {
                const row = currentSheetData[originalRowIndex - 1];
                if (!row.values) row.values = [];
                
                while (row.values.length <= 6) {
                    row.values.push({});
                }
                
                row.values[6] = {
                    effectiveValue: { stringValue: formattedDate }
                };

                localStorage.setItem('cached_sheet_data', JSON.stringify(currentSheetData));
                
                // Rerender table
                renderTable(currentSheetData);
            }

        } catch (err) {
            log(`ERROR committing cross border date: ${err.message}`);
            alert(`Failed to commit date: ${err.message}`);
            dateInput.disabled = false;
            commitBtn.disabled = false;
            commitBtn.innerHTML = '<i class="fa-solid fa-truck-fast me-1"></i>Cross';
        }
    }

    function renderTable(rowData) {
        tableHeader.innerHTML = '';
        tableBody.innerHTML = '';

        if (!rowData || rowData.length === 0) return;

        // Tag each row with its original spreadsheet row index (1-based)
        rowData.forEach((row, idx) => {
            row.originalRowIndex = idx + 1;
        });

        const searchTerm = searchInput.value.toLowerCase().trim();

        // Get header indices
        const headers = rowData[0].values.map(v => (v.effectiveValue?.stringValue || '').toUpperCase());
        const targetCols = [
            "CLIENT", "INV NO", "REF NO", "TYPE", "INV DAT", 
            "EXPRESS CO", "CONTAINER", "BILL", "PALLET: GROSS"
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
            
            if (rowIndex > 0) {
                let hasGreenRefNo = false;
                const refNoCol = colMap.find(c => c.name === "REF NO");
                if (refNoCol) {
                    const cell = row.values[refNoCol.index];
                    if (cell) {
                        const bg = cell.effectiveFormat?.backgroundColor;
                        if (bg) {
                            const r = Math.round((bg.red || 0) * 255);
                            const g = Math.round((bg.green || 0) * 255);
                            const b = Math.round((bg.blue || 0) * 255);
                            if (g > r && g > b && g > 100) {
                                hasGreenRefNo = true;
                            }
                        }
                    }
                }
                if (!hasGreenRefNo) {
                    tr.classList.add('warning-row');
                }
            }
            
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
                            val = formatDateToDDMMYY(date);
                        } else {
                            val = eff.numberValue;
                        }
                    }
                    else if (eff.boolValue !== undefined) val = eff.boolValue;
                }
                el.textContent = val;

                // Get Background Color
                const bg = cell.effectiveFormat?.backgroundColor;
                let hasCustomBg = false;
                if (bg && rowIndex > 0) { // Only apply spreadsheet colors to data rows
                    const r = Math.round((bg.red || 0) * 255);
                    const g = Math.round((bg.green || 0) * 255);
                    const b = Math.round((bg.blue || 0) * 255);
                    // Check if background is not white
                    if (r < 255 || g < 255 || b < 255) {
                        hasCustomBg = true;
                        el.style.backgroundColor = `rgb(${r},${g},${b})`;
                        // Basic contrast check
                        const brightness = (r * 299 + g * 587 + b * 114) / 1000;
                        if (brightness < 128) el.style.color = 'white';
                    }
                }

                if (rowIndex === 0) {
                    tableHeader.appendChild(el);
                } else {
                    el.setAttribute('data-label', col.name);
                    if (!val && !hasCustomBg) {
                        el.classList.add('empty-cell');
                    }
                    tr.appendChild(el);
                }
            });

            if (rowIndex === 0) {
                const actionTh = document.createElement('th');
                actionTh.textContent = 'CROSS BORDER';
                actionTh.style.backgroundColor = '#34495e';
                actionTh.style.color = 'white';
                actionTh.style.textAlign = 'center';
                actionTh.style.fontWeight = '600';
                actionTh.style.padding = '15px 10px';
                tableHeader.appendChild(actionTh);
            } else {
                const actionTd = document.createElement('td');
                actionTd.className = 'action-cell';
                
                // Find invoice number
                let invoiceId = '';
                const invCol = colMap.find(c => c.name === "INV NO");
                if (invCol && row.values[invCol.index]) {
                    invoiceId = row.values[invCol.index].effectiveValue?.stringValue || '';
                }

                // Check if Column G (index 6) already has a value
                const cellG = row.values && row.values.length > 6 ? row.values[6] : null;
                let existingDateVal = '';
                if (cellG && cellG.effectiveValue) {
                    if (cellG.effectiveValue.stringValue) {
                        existingDateVal = cellG.effectiveValue.stringValue;
                    } else if (cellG.effectiveValue.numberValue !== undefined) {
                        const serial = cellG.effectiveValue.numberValue;
                        if (serial > 30000 && serial < 60000) {
                            const date = new Date((serial - 25569) * 86400 * 1000);
                            existingDateVal = formatDateToDDMMYY(date);
                        } else {
                            existingDateVal = String(serial);
                        }
                    } else if (cellG.effectiveValue.boolValue !== undefined) {
                        existingDateVal = String(cellG.effectiveValue.boolValue);
                    }
                }

                // Create container div for styling
                const container = document.createElement('div');
                container.className = 'd-flex align-items-center justify-content-between w-100 py-1';

                // Label for mobile view (hidden on desktop)
                const label = document.createElement('span');
                label.className = 'd-md-none fw-bold text-muted small text-uppercase';
                label.style.letterSpacing = '0.5px';
                label.style.fontSize = '0.75rem';
                label.textContent = 'CROSS BORDER';
                container.appendChild(label);

                if (existingDateVal && existingDateVal.trim() !== '') {
                    // Badge text indicating container crossed the border
                    const badge = document.createElement('span');
                    badge.className = 'badge bg-success-subtle text-success border border-success-subtle py-1.5 px-3 ms-auto';
                    badge.style.fontSize = '0.8rem';
                    badge.style.fontWeight = '600';
                    badge.innerHTML = `<i class="fa-solid fa-circle-check me-1"></i>${existingDateVal}`;
                    
                    container.appendChild(badge);
                } else {
                    // Controls wrapper for inputs
                    const controls = document.createElement('div');
                    controls.className = 'd-flex align-items-center gap-2 ms-auto';

                    const dateInput = document.createElement('input');
                    dateInput.type = 'date';
                    dateInput.className = 'form-control form-control-sm border-date-input';
                    dateInput.style.width = '145px';
                    dateInput.style.padding = '4px 8px';
                    dateInput.style.fontSize = '0.85rem';
                    
                    const today = new Date();
                    const year = today.getFullYear();
                    const month = String(today.getMonth() + 1).padStart(2, '0');
                    const day = String(today.getDate()).padStart(2, '0');
                    dateInput.value = `${year}-${month}-${day}`;

                    const commitBtn = document.createElement('button');
                    commitBtn.className = 'btn btn-xs btn-primary commit-date-btn py-1.5 px-3';
                    commitBtn.style.fontSize = '0.8rem';
                    commitBtn.style.whiteSpace = 'nowrap';
                    commitBtn.innerHTML = '<i class="fa-solid fa-truck-fast me-1"></i>Cross';
                    
                    commitBtn.addEventListener('click', (e) => {
                        e.stopPropagation();
                        const selectedDate = dateInput.value;
                        if (!selectedDate) {
                            alert("Please select a date first.");
                            return;
                        }
                        
                        const confirmMsg = `Are you sure you want to mark Invoice "${invoiceId}" as crossed border on ${selectedDate}?`;
                        if (!confirm(confirmMsg)) {
                            return;
                        }
                        
                        commitCrossBorderDate(invoiceId, row.originalRowIndex, selectedDate, dateInput, commitBtn, actionTd);
                    });

                    controls.appendChild(dateInput);
                    controls.appendChild(commitBtn);
                    container.appendChild(controls);
                }

                actionTd.appendChild(container);
                tr.appendChild(actionTd);
            }

            if (rowIndex > 0) tableBody.appendChild(tr);
        });
    }

    // Call this at the very end after all functions are defined
    loadSettings();
});
