document.addEventListener('DOMContentLoaded', () => {
    // DOM Elements
    const geminiKeyInput = document.getElementById('geminiKey');
    const geminiModelInput = document.getElementById('geminiModel');
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
    const clearSearchInputBtn = document.getElementById('clearSearchInput');
    const editModeToggleBtn = document.getElementById('editModeToggleBtn');
    const tabTitleText = document.getElementById('tabTitleText');
    const tableTitleIcon = document.getElementById('tableTitleIcon');
    const logContainer = document.getElementById('log');
    
    const tableHeader = document.getElementById('tableHeader');
    const tableBody = document.getElementById('tableBody');

    // Red Invoice Alert DOM Elements
    const redInvoiceAlertSection = document.getElementById('redInvoiceAlertSection');
    const redInvoiceCount = document.getElementById('redInvoiceCount');
    const toggleRedInvoiceListBtn = document.getElementById('toggleRedInvoiceList');
    const redInvoiceListContainer = document.getElementById('redInvoiceListContainer');
    const redInvoiceBadges = document.getElementById('redInvoiceBadges');

    // Green Invoice Alert DOM Elements
    const greenInvoiceAlertSection = document.getElementById('greenInvoiceAlertSection');
    const greenInvoiceCount = document.getElementById('greenInvoiceCount');
    const toggleGreenInvoiceListBtn = document.getElementById('toggleGreenInvoiceList');
    const greenInvoiceListContainer = document.getElementById('greenInvoiceListContainer');
    const greenInvoiceBadges = document.getElementById('greenInvoiceBadges');

    // Multi-Sheet Tabs Configuration
    const TABS_CONFIG = [
        {
            id: '2026',
            sheetName: '2026',
            tabTitle: '2026',
            previewTitle: '2026 Live Preview',
            icon: 'fa-solid fa-ship',
            cacheKey: 'cached_sheet_data_2026',
            fetchRange: '2026!A1:Z500',
            crossColLetter: 'G',
            crossColIndex: 6, // 0-indexed Column G
            refNoColIndex: 3, // 0-indexed Column D
            actionColTitle: 'CROSS BORDER',
            targetCols: ["CLIENT", "IFL-CLIENT", "INV NO", "REF NO", "INV DAT", "CONTAINER", "BILL"],
            allowContainerDelete: true
        },
        {
            id: 'local_supply',
            sheetName: 'LOCAL  SUPPLY', // Exact sheet name with 2 spaces
            tabTitle: 'LOCAL EXPORT',
            previewTitle: 'LOCAL EXPORT Live Preview',
            icon: 'fa-solid fa-truck',
            cacheKey: 'cached_sheet_data_local_supply',
            fetchRange: "'LOCAL  SUPPLY'!A1:Z500",
            crossColLetter: 'J',
            crossColIndex: 9, // 0-indexed Column J
            refNoColIndex: 6, // 0-indexed Column G
            actionColTitle: 'CROSS',
            targetCols: ["N.O", "CLIENT", "INV NO", "REF NO", "INV DAT"],
            allowContainerDelete: false
        }
    ];

    let currentTabIndex = parseInt(localStorage.getItem('active_tab_index') || '0', 10);
    if (isNaN(currentTabIndex) || currentTabIndex < 0 || currentTabIndex >= TABS_CONFIG.length) {
        currentTabIndex = 0;
    }

    let isEditMode = false;
    let currentSheetData = null; // Store fetched data for active tab
    let selectedFile = null;     // Store selected/dropped/pasted image

    // Constants
    const PROMPT = `
    Analyze shipping spreadsheet image.
    Extract all data rows. Map to exactly these 10 columns in order:
    
    1. TBL NO (If header is "Booking", map to this. This is the BILL)
    2. SHIPPER (The invoice ID)
    3. CONTAINER NO. (If header is "Container no", map to this. If content length < 11 chars, replace with "Headtruck" or "TRUCK NO." value)
    4. TYPE
    5. SEAL NO.
    6. TRUCK NO. (If header is "Headtruck", map to this. If content is truck size, replace with plate no.)
    7. DRIVER NAME
    8. CNEE
    9. DATE
    10. PALLET: GROSS (The pallet gross weight, often labeled "p: gross" or "pallet gross" or similar. Extract the raw weight value, e.g. "1234.56" or "1234")

    Return ONLY CSV format, one row per line. No headers. No labels.
    Use comma separator. Omit or replace internal commas with space.
    `;

    // Date formatting helper: DD-MMM-YYYY (e.g. 02-Jan-2026)
    function formatDateDDMMMYYYY(date) {
        if (!date || isNaN(date.getTime())) return '';
        const day = String(date.getUTCDate()).padStart(2, '0');
        const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
        const month = months[date.getUTCMonth()];
        const year = date.getUTCFullYear();
        return `${day}-${month}-${year}`;
    }

    function serialToFormattedDate(serial) {
        if (typeof serial === 'number' && serial > 30000 && serial < 60000) {
            const date = new Date(Math.round((serial - 25569) * 86400 * 1000));
            const day = String(date.getUTCDate()).padStart(2, '0');
            const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
            const month = months[date.getUTCMonth()];
            const year = date.getUTCFullYear();
            return `${day}-${month}-${year}`;
        }
        return String(serial);
    }

    function isRedColor(bg) {
        if (!bg) return false;
        const r = Math.round((bg.red || 0) * 255);
        const g = Math.round((bg.green || 0) * 255);
        const b = Math.round((bg.blue || 0) * 255);
        // Red color check: red is dominant and above threshold
        return (r > g + 15 && r > b + 15);
    }

    function isGreenColor(bg) {
        if (!bg) return false;
        const r = Math.round((bg.red || 0) * 255);
        const g = Math.round((bg.green || 0) * 255);
        const b = Math.round((bg.blue || 0) * 255);
        // Green color check: green is dominant and above threshold
        return (g > r + 15 && g > b + 15);
    }

    // Switch Tab and load data
    function switchTab(newIndex) {
        currentTabIndex = newIndex;
        localStorage.setItem('active_tab_index', String(currentTabIndex));

        // Update nav-pills active class
        const tabBtns = document.querySelectorAll('#sheetTabs .nav-link');
        tabBtns.forEach((btn, idx) => {
            if (idx === currentTabIndex) {
                btn.classList.add('active');
            } else {
                btn.classList.remove('active');
            }
        });

        const activeConfig = TABS_CONFIG[currentTabIndex];
        if (tabTitleText) {
            tabTitleText.textContent = activeConfig.previewTitle;
        }
        if (tableTitleIcon) {
            tableTitleIcon.className = `${activeConfig.icon} me-2 text-primary`;
        }

        // Load cached data for the newly selected tab
        const cachedData = localStorage.getItem(activeConfig.cacheKey);
        if (cachedData) {
            try {
                currentSheetData = JSON.parse(cachedData);
                renderTable(currentSheetData);
                log(`Loaded cached data for [${activeConfig.tabTitle}].`);
            } catch (e) {
                console.error("Failed to parse cached data for tab", e);
                currentSheetData = null;
                renderTable(null);
            }
        } else {
            currentSheetData = null;
            renderTable(null);
            log(`No local cache for [${activeConfig.tabTitle}]. Click "Refresh Data" to load.`);
        }
    }

    // Setup Tab Buttons
    const tabBtns = document.querySelectorAll('#sheetTabs .nav-link');
    tabBtns.forEach(btn => {
        btn.addEventListener('click', () => {
            const tabIdx = parseInt(btn.getAttribute('data-tab-index'), 10);
            switchTab(tabIdx);
        });
    });

    // Initialize Settings and Caches
    function loadSettings() {
        geminiKeyInput.value = localStorage.getItem('gemini_api_key') || '';
        geminiModelInput.value = localStorage.getItem('gemini_model') || 'gemini-3.1-flash-lite-preview';
        spreadsheetIdInput.value = localStorage.getItem('spreadsheet_id') || '';
        serviceAccountInput.value = localStorage.getItem('service_account_json') || '';

        // Legacy cache migration
        const legacyCached = localStorage.getItem('cached_sheet_data');
        if (legacyCached && !localStorage.getItem('cached_sheet_data_2026')) {
            localStorage.setItem('cached_sheet_data_2026', legacyCached);
        }

        // Load active tab
        switchTab(currentTabIndex);
    }

    // Edit Mode Toggle
    if (editModeToggleBtn) {
        editModeToggleBtn.addEventListener('click', () => {
            isEditMode = !isEditMode;
            if (isEditMode) {
                editModeToggleBtn.classList.remove('btn-outline-warning');
                editModeToggleBtn.classList.add('btn-warning');
                editModeToggleBtn.innerHTML = '<i class="fa-solid fa-check me-1"></i>Edit Mode: ON';
            } else {
                editModeToggleBtn.classList.remove('btn-warning');
                editModeToggleBtn.classList.add('btn-outline-warning');
                editModeToggleBtn.innerHTML = '<i class="fa-solid fa-pen-to-square me-1"></i>Edit Mode';
            }
            if (currentSheetData) {
                renderTable(currentSheetData);
            }
        });
    }

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

        if (e.clipboardData.files && e.clipboardData.files.length > 0) {
            handleImageFile(e.clipboardData.files[0]);
            e.preventDefault();
        }
    });

    imagePathInput.addEventListener('input', () => {
        if (imagePathInput.value.trim()) {
            selectedFile = null;
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
        imagePathInput.value = '';
        
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
        
        if (window.innerWidth < 768) {
            dataEntryBody.classList.add('hidden');
            toggleDataEntry.innerHTML = '<i class="fa-solid fa-plus me-1"></i>Expand';
        }
    }

    // Toggle Red Invoice Alert List
    if (toggleRedInvoiceListBtn && redInvoiceListContainer) {
        toggleRedInvoiceListBtn.addEventListener('click', () => {
            redInvoiceListContainer.classList.toggle('hidden');
            if (redInvoiceListContainer.classList.contains('hidden')) {
                toggleRedInvoiceListBtn.innerHTML = '<i class="fa-solid fa-list me-1"></i>Show';
            } else {
                toggleRedInvoiceListBtn.innerHTML = '<i class="fa-solid fa-chevron-up me-1"></i>Hide';
            }
        });
    }

    // Toggle Green Invoice Alert List
    if (toggleGreenInvoiceListBtn && greenInvoiceListContainer) {
        toggleGreenInvoiceListBtn.addEventListener('click', () => {
            greenInvoiceListContainer.classList.toggle('hidden');
            if (greenInvoiceListContainer.classList.contains('hidden')) {
                toggleGreenInvoiceListBtn.innerHTML = '<i class="fa-solid fa-list me-1"></i>Show';
            } else {
                toggleGreenInvoiceListBtn.innerHTML = '<i class="fa-solid fa-chevron-up me-1"></i>Hide';
            }
        });
    }

    if (clearSearchInputBtn) {
        clearSearchInputBtn.addEventListener('click', () => {
            searchInput.value = '';
            clearSearchInputBtn.classList.add('hidden');
            if (currentSheetData) {
                renderTable(currentSheetData);
            }
        });
    }

    searchInput.addEventListener('input', () => {
        if (clearSearchInputBtn) {
            if (searchInput.value.trim()) {
                clearSearchInputBtn.classList.remove('hidden');
            } else {
                clearSearchInputBtn.classList.add('hidden');
            }
        }
        if (currentSheetData) {
            renderTable(currentSheetData);
        }
    });

    saveSettingsBtn.addEventListener('click', () => {
        localStorage.setItem('gemini_api_key', geminiKeyInput.value);
        localStorage.setItem('gemini_model', geminiModelInput.value);
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
        const geminiModel = geminiModelInput.value.trim();
        const spreadsheetId = spreadsheetIdInput.value;
        const saJson = serviceAccountInput.value;
        const urlInput = imagePathInput.value.trim();

        if ((!selectedFile && !urlInput) || !invoiceIds.length || !geminiKey || !geminiModel || !spreadsheetId || !saJson) {
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
            const extractedRows = await callGemini(geminiKey, base64Image, invoiceIds, geminiModel);
            log(`Extracted ${extractedRows.length} rows.`);

            log("Authenticating with Google Sheets...");
            const accessToken = await getGoogleAccessToken(JSON.parse(saJson));
            
            log("Pushing to Google Sheets CONTAINER sheet...");
            await appendToGoogleSheets(accessToken, spreadsheetId, extractedRows);
            log("Successfully pushed to Google Sheets.");

            log("Fetching updated sheet data for all tabs...");
            await fetchAndRenderSheet(accessToken, spreadsheetId);

        } catch (err) {
            log(`ERROR: ${err.message}`);
            console.error(err);
        } finally {
            processBtn.disabled = false;
            processBtn.innerHTML = '<i class="fa-solid fa-bolt me-1"></i>Extract & Sync';
        }
    });

    fetchBtn.addEventListener('click', async () => {
        const spreadsheetId = spreadsheetIdInput.value;
        const saJson = serviceAccountInput.value;
        if (!spreadsheetId || !saJson) {
            alert("Please provide Spreadsheet ID and Service Account JSON in Settings.");
            return;
        }

        try {
            fetchBtn.disabled = true;
            fetchBtn.innerHTML = '<i class="fa-solid fa-spinner fa-spin me-1"></i>Fetching...';
            log("Authenticating with Google Sheets API...");
            const accessToken = await getGoogleAccessToken(JSON.parse(saJson));
            log("Fetching sheet data for all tabs...");
            await fetchAndRenderSheet(accessToken, spreadsheetId);
        } catch (err) {
            log(`ERROR: ${err.message}`);
        } finally {
            fetchBtn.disabled = false;
            fetchBtn.innerHTML = '<i class="fa-solid fa-rotate me-1"></i>Refresh Data';
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

    async function callGemini(apiKey, base64Image, clientIds, modelName) {
        const model = modelName || 'gemini-3.1-flash-lite-preview';
        const url = `https://generativelanguage.googleapis.com/v1beta/models/${model}:generateContent?key=${apiKey}`;
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
            if (cols.length === 9 && /^[A-Z]{4}-?\d{7}$/i.test(cols[1])) {
                cols.unshift('');
            }
            while (cols.length < 10) {
                cols.push('');
            }
            const finalCols = cols.slice(0, 10);
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

    // Fetch and cache sheet data for all tabs in a single batch request
    async function fetchAndRenderSheet(token, spreadsheetId) {
        const range2026 = encodeURIComponent(TABS_CONFIG[0].fetchRange);
        const rangeLocal = encodeURIComponent(TABS_CONFIG[1].fetchRange);
        const url = `https://sheets.googleapis.com/v4/spreadsheets/${spreadsheetId}?ranges=${range2026}&ranges=${rangeLocal}&fields=sheets(properties(title),data(rowData(values(effectiveValue,effectiveFormat(backgroundColor)))))`;

        try {
            const resp = await fetch(url, {
                headers: { 'Authorization': `Bearer ${token}` }
            });

            if (!resp.ok) {
                // Fallback to fetch single active tab
                return await fetchSingleTab(token, spreadsheetId, currentTabIndex);
            }

            const data = await resp.json();
            if (data.sheets && data.sheets.length > 0) {
                data.sheets.forEach(sheet => {
                    const title = sheet.properties?.title;
                    const rowData = sheet.data && sheet.data[0] ? (sheet.data[0].rowData || []) : [];
                    const matchingTab = TABS_CONFIG.find(t => t.sheetName === title);
                    if (matchingTab) {
                        localStorage.setItem(matchingTab.cacheKey, JSON.stringify(rowData));
                        if (matchingTab === TABS_CONFIG[currentTabIndex]) {
                            currentSheetData = rowData;
                        }
                    }
                });
                renderTable(currentSheetData);
                log(`Fetched & cached updated data for all sheets.`);
            }
        } catch (e) {
            log(`Multi-sheet fetch fallback: ${e.message}`);
            await fetchSingleTab(token, spreadsheetId, currentTabIndex);
        }
    }

    async function fetchSingleTab(token, spreadsheetId, tabIdx) {
        const tab = TABS_CONFIG[tabIdx];
        const url = `https://sheets.googleapis.com/v4/spreadsheets/${spreadsheetId}?ranges=${encodeURIComponent(tab.fetchRange)}&fields=sheets(data(rowData(values(effectiveValue,effectiveFormat(backgroundColor)))))`;

        const resp = await fetch(url, {
            headers: { 'Authorization': `Bearer ${token}` }
        });

        if (!resp.ok) {
            const error = await resp.json();
            throw new Error(`Google Sheets Fetch error: ${error.error?.message || error.error || resp.statusText}`);
        }

        const data = await resp.json();
        const sheet = data.sheets[0];
        const rowData = sheet.data && sheet.data[0] ? (sheet.data[0].rowData || []) : [];
        currentSheetData = rowData;
        localStorage.setItem(tab.cacheKey, JSON.stringify(rowData));
        renderTable(currentSheetData);
        log(`Fetched & cached data for [${tab.tabTitle}].`);
    }

    // Commit Cross Border Date to specific sheet column
    async function commitCrossBorderDate(tab, invoiceId, originalRowIndex, selectedDate, dateInput, commitBtn, actionTd) {
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
            log(`Marking invoice ${invoiceId} as crossed on ${tab.tabTitle} for ${selectedDate}...`);

            const token = await getGoogleAccessToken(JSON.parse(saJson));

            // Quote sheet name if needed
            const sheetRef = tab.sheetName.includes(' ') ? `'${tab.sheetName}'` : tab.sheetName;
            const range = `${sheetRef}!${tab.crossColLetter}${originalRowIndex}`;
            const url = `https://sheets.googleapis.com/v4/spreadsheets/${spreadsheetId}/values/${encodeURIComponent(range)}?valueInputOption=USER_ENTERED`;

            const resp = await fetch(url, {
                method: 'PUT',
                headers: {
                    'Authorization': `Bearer ${token}`,
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({
                    values: [[selectedDate]]
                })
            });

            if (!resp.ok) {
                const error = await resp.json();
                throw new Error(`Google Sheets Update error: ${error.error?.message || error.error || resp.statusText}`);
            }

            log(`Successfully committed date ${selectedDate} for Invoice ${invoiceId} on ${tab.tabTitle}.`);

            // Update in-memory data and localStorage cache
            if (currentSheetData && currentSheetData[originalRowIndex - 1]) {
                const row = currentSheetData[originalRowIndex - 1];
                if (!row.values) row.values = [];
                
                while (row.values.length <= tab.crossColIndex) {
                    row.values.push({});
                }
                
                row.values[tab.crossColIndex] = {
                    effectiveValue: { stringValue: selectedDate }
                };

                localStorage.setItem(tab.cacheKey, JSON.stringify(currentSheetData));
                renderTable(currentSheetData);
            }

        } catch (err) {
            log(`ERROR committing cross date: ${err.message}`);
            alert(`Failed to commit date: ${err.message}`);
            dateInput.disabled = false;
            commitBtn.disabled = false;
            commitBtn.innerHTML = '<i class="fa-solid fa-truck-fast me-sm-1"></i><span class="d-none d-sm-inline">Cross</span>';
        }
    }

    // Container Deletion / Clear Feature from CONTAINER sheet
    async function deleteContainerRow(invoiceId) {
        const spreadsheetId = spreadsheetIdInput.value;
        const saJson = serviceAccountInput.value;
        if (!spreadsheetId || !saJson) {
            alert("Please provide Spreadsheet ID and Service Account JSON in Settings.");
            return;
        }

        if (!invoiceId || !invoiceId.trim()) {
            alert("No Invoice ID found to delete container rows.");
            return;
        }

        const confirmMsg = `Are you sure you want to delete all container records for Invoice "${invoiceId}" from the CONTAINER sheet?`;
        if (!confirm(confirmMsg)) {
            return;
        }

        try {
            log(`Locating container records for Invoice "${invoiceId}"...`);
            const token = await getGoogleAccessToken(JSON.parse(saJson));

            // 1. Fetch spreadsheet metadata to find sheetId of CONTAINER sheet
            const metaUrl = `https://sheets.googleapis.com/v4/spreadsheets/${spreadsheetId}?fields=sheets(properties(sheetId,title))`;
            const metaResp = await fetch(metaUrl, {
                headers: { 'Authorization': `Bearer ${token}` }
            });
            if (!metaResp.ok) {
                const error = await metaResp.json();
                throw new Error(`Failed to fetch sheet metadata: ${error.error?.message || metaResp.statusText}`);
            }
            const metaData = await metaResp.json();
            const containerSheet = (metaData.sheets || []).find(s => s.properties?.title === 'CONTAINER');
            if (!containerSheet) {
                throw new Error('Sheet "CONTAINER" was not found in this spreadsheet.');
            }
            const containerSheetId = containerSheet.properties.sheetId;

            // 2. Fetch CONTAINER!B:B to find matching row indices
            const valuesUrl = `https://sheets.googleapis.com/v4/spreadsheets/${spreadsheetId}/values/CONTAINER!B:B`;
            const valuesResp = await fetch(valuesUrl, {
                headers: { 'Authorization': `Bearer ${token}` }
            });
            if (!valuesResp.ok) {
                const error = await valuesResp.json();
                throw new Error(`Failed to fetch CONTAINER sheet rows: ${error.error?.message || valuesResp.statusText}`);
            }
            const valuesData = await valuesResp.json();
            const rows = valuesData.values || [];

            const matchingIndices = [];
            const normalizedTarget = invoiceId.trim().toUpperCase();

            rows.forEach((row, idx) => {
                const cellVal = (row[0] || '').toString().trim().toUpperCase();
                if (cellVal === normalizedTarget) {
                    matchingIndices.push(idx); // 0-based index
                }
            });

            if (matchingIndices.length === 0) {
                log(`No matching records found for Invoice "${invoiceId}" in CONTAINER sheet.`);
                alert(`No container records found for Invoice "${invoiceId}" in CONTAINER sheet.`);
                return;
            }

            // Sort descending to prevent row index shifting during execution
            matchingIndices.sort((a, b) => b - a);

            log(`Found ${matchingIndices.length} row(s) for Invoice "${invoiceId}". Deleting from CONTAINER sheet...`);

            // 3. Build deleteDimension batchUpdate requests
            const requests = matchingIndices.map(rowIdx => ({
                deleteDimension: {
                    range: {
                        sheetId: containerSheetId,
                        dimension: 'ROWS',
                        startIndex: rowIdx,
                        endIndex: rowIdx + 1
                    }
                }
            }));

            const batchUrl = `https://sheets.googleapis.com/v4/spreadsheets/${spreadsheetId}:batchUpdate`;
            const batchResp = await fetch(batchUrl, {
                method: 'POST',
                headers: {
                    'Authorization': `Bearer ${token}`,
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({ requests })
            });

            if (!batchResp.ok) {
                const error = await batchResp.json();
                throw new Error(`Batch delete failed: ${error.error?.message || batchResp.statusText}`);
            }

            log(`Successfully deleted ${matchingIndices.length} container row(s) for Invoice "${invoiceId}" from CONTAINER sheet.`);
            alert(`Successfully deleted ${matchingIndices.length} container row(s) for Invoice "${invoiceId}".`);

            // Re-fetch sheets to update cache and view
            await fetchAndRenderSheet(token, spreadsheetId);
        } catch (err) {
            log(`ERROR deleting container rows: ${err.message}`);
            alert(`Error deleting container rows: ${err.message}`);
        }
    }

    function renderTable(rowData) {
        tableHeader.innerHTML = '';
        tableBody.innerHTML = '';

        const tab = TABS_CONFIG[currentTabIndex];

        if (!rowData || rowData.length === 0) {
            const tr = document.createElement('tr');
            const td = document.createElement('td');
            td.colSpan = 10;
            td.className = 'text-center text-muted py-4';
            td.innerHTML = `<i class="fa-solid fa-inbox me-2"></i>No data loaded for ${tab.tabTitle}. Click "Refresh Data" to load.`;
            tr.appendChild(td);
            tableBody.appendChild(tr);

            if (redInvoiceAlertSection) redInvoiceAlertSection.classList.add('hidden');
            if (greenInvoiceAlertSection) greenInvoiceAlertSection.classList.add('hidden');
            return;
        }

        // Tag each row with its original spreadsheet row index (1-based)
        rowData.forEach((row, idx) => {
            row.originalRowIndex = idx + 1;
        });

        const searchTerm = searchInput.value.toLowerCase().trim();

        // Get headers from first row
        const headers = rowData[0].values ? rowData[0].values.map(v => (v.effectiveValue?.stringValue || '').toUpperCase().trim()) : [];
        const targetCols = tab.targetCols;
        const colMap = [];

        targetCols.forEach(tc => {
            let colIndex = -1;
            if (headers && headers.length > 0) {
                if (tc === "N.O") {
                    colIndex = headers.findIndex(h => h === "N.O" || h === "NO" || h === "NO." || h.includes("N.O") || h.includes("NO."));
                } else if (tc === "CLIENT") {
                    colIndex = headers.findIndex(h => h === "CLIENT" || h === "CLIENT NAME" || h === "CUSTOMER");
                    if (colIndex === -1) {
                        colIndex = headers.findIndex(h => h.includes("CLIENT") && !h.includes("IFL"));
                    }
                } else if (tc === "IFL-CLIENT") {
                    colIndex = headers.findIndex(h => h.includes("IFL-CLIENT") || h.includes("IFL CLIENT") || h.includes("IFL"));
                } else if (tc === "INV DAT") {
                    colIndex = headers.findIndex(h => h.includes("INV DAT") || h.includes("INV DATE") || h.includes("DATE"));
                } else if (tc === "INV NO") {
                    colIndex = headers.findIndex(h => h.includes("INV NO") || h.includes("INVOICE") || h.includes("INV"));
                } else if (tc === "REF NO") {
                    colIndex = headers.findIndex(h => h.includes("REF NO") || h.includes("REF"));
                } else {
                    colIndex = headers.findIndex(h => h.includes(tc));
                }
            }

            // Fallback indices if headers did not match
            if (colIndex === -1) {
                if (tab.sheetName === 'LOCAL  SUPPLY') {
                    if (tc === "N.O") colIndex = 0;
                    else if (tc === "CLIENT") colIndex = 1;
                    else if (tc === "INV NO") colIndex = 5;
                    else if (tc === "REF NO") colIndex = 6;
                    else if (tc === "INV DAT") colIndex = 8;
                } else {
                    if (tc === "CLIENT") colIndex = 0;
                    else if (tc === "IFL-CLIENT") colIndex = 1;
                    else if (tc === "INV NO") colIndex = 2;
                    else if (tc === "REF NO") colIndex = 3;
                    else if (tc === "INV DAT") colIndex = 4;
                    else if (tc === "CONTAINER") colIndex = 7;
                    else if (tc === "BILL") colIndex = 8;
                }
            }

            if (colIndex !== -1) {
                colMap.push({ name: tc, index: colIndex });
            }
        });

        // Display rows in descending order (reverse), but keep header at top
        const dataRows = rowData.slice(1).reverse();

        // Flagged Red & Green Invoices
        const invCol = colMap.find(c => c.name === "INV NO") || { index: tab.sheetName === 'LOCAL  SUPPLY' ? 5 : 2 };
        const refNoColIdx = tab.refNoColIndex;
        const crossColIdx = tab.crossColIndex;
        let redInvoices = [];
        let greenInvoices = [];

        dataRows.forEach(row => {
            if (row.values) {
                let invoiceVal = '';
                if (row.values.length > invCol.index && row.values[invCol.index]) {
                    const invCell = row.values[invCol.index];
                    invoiceVal = invCell.effectiveValue?.stringValue || (invCell.effectiveValue?.numberValue !== undefined ? String(invCell.effectiveValue.numberValue) : '');
                }

                // Red alert on REF NO
                if (row.values.length > refNoColIdx) {
                    const refCell = row.values[refNoColIdx];
                    const bg = refCell?.effectiveFormat?.backgroundColor;
                    if (isRedColor(bg) && invoiceVal) {
                        redInvoices.push(invoiceVal);
                    }
                }

                // Green alert on Cross column
                if (row.values.length > crossColIdx) {
                    const crossCell = row.values[crossColIdx];
                    const bg = crossCell?.effectiveFormat?.backgroundColor;
                    if (isGreenColor(bg) && invoiceVal) {
                        greenInvoices.push(invoiceVal);
                    }
                }
            }
        });

        // Display or hide red invoice alert section
        if (redInvoices.length > 0 && redInvoiceAlertSection && redInvoiceCount && redInvoiceBadges) {
            redInvoiceCount.textContent = redInvoices.length;
            redInvoiceAlertSection.classList.remove('hidden');
            redInvoiceBadges.innerHTML = '';
            const uniqueRedInvoices = [...new Set(redInvoices)];
            uniqueRedInvoices.forEach(invNo => {
                const badge = document.createElement('span');
                badge.className = 'badge-clickable-red';
                badge.innerHTML = `<i class="fa-solid fa-triangle-exclamation"></i> ${invNo}`;
                badge.addEventListener('click', () => {
                    searchInput.value = invNo;
                    searchInput.dispatchEvent(new Event('input'));
                    const targetTable = document.querySelector('.table-container');
                    if (targetTable) {
                        targetTable.scrollIntoView({ behavior: 'smooth', block: 'start' });
                    }
                });
                redInvoiceBadges.appendChild(badge);
            });
        } else if (redInvoiceAlertSection) {
            redInvoiceAlertSection.classList.add('hidden');
        }

        // Display or hide green invoice alert section
        if (greenInvoices.length > 0 && greenInvoiceAlertSection && greenInvoiceCount && greenInvoiceBadges) {
            greenInvoiceCount.textContent = greenInvoices.length;
            greenInvoiceAlertSection.classList.remove('hidden');
            greenInvoiceBadges.innerHTML = '';
            const uniqueGreenInvoices = [...new Set(greenInvoices)];
            uniqueGreenInvoices.forEach(invNo => {
                const badge = document.createElement('span');
                badge.className = 'badge-clickable-green';
                badge.innerHTML = `<i class="fa-solid fa-circle-check"></i> ${invNo}`;
                badge.addEventListener('click', () => {
                    searchInput.value = invNo;
                    searchInput.dispatchEvent(new Event('input'));
                    const targetTable = document.querySelector('.table-container');
                    if (targetTable) {
                        targetTable.scrollIntoView({ behavior: 'smooth', block: 'start' });
                    }
                });
                greenInvoiceBadges.appendChild(badge);
            });
        } else if (greenInvoiceAlertSection) {
            greenInvoiceAlertSection.classList.add('hidden');
        }

        // Render Table Headers
        colMap.forEach(col => {
            const th = document.createElement('th');
            th.textContent = col.name;
            tableHeader.appendChild(th);
        });

        const actionTh = document.createElement('th');
        actionTh.textContent = tab.actionColTitle;
        actionTh.style.backgroundColor = '#34495e';
        actionTh.style.color = 'white';
        actionTh.style.textAlign = 'center';
        actionTh.style.fontWeight = '600';
        actionTh.style.padding = '15px 10px';
        tableHeader.appendChild(actionTh);

        // Apply search filter if active
        let filteredDataRows = dataRows.filter(row => {
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

        // Render Table Data Rows
        filteredDataRows.forEach(row => {
            if (!row.values) return;
            const tr = document.createElement('tr');
            
            // Highlight unpassed shipments (REF NO not green)
            let hasGreenRefNo = false;
            if (row.values.length > refNoColIdx) {
                const cell = row.values[refNoColIdx];
                if (cell) {
                    const bg = cell.effectiveFormat?.backgroundColor;
                    if (isGreenColor(bg)) {
                        hasGreenRefNo = true;
                    }
                }
            }
            if (!hasGreenRefNo) {
                tr.classList.add('warning-row');
            }

            // Render columns in colMap
            colMap.forEach(col => {
                const cell = row.values && row.values.length > col.index ? row.values[col.index] : null;
                const el = document.createElement('td');
                el.setAttribute('data-label', col.name);

                let val = '';
                if (cell && cell.effectiveValue) {
                    const eff = cell.effectiveValue;
                    if (eff.stringValue) {
                        val = eff.stringValue;
                    } else if (eff.numberValue !== undefined) {
                        const num = eff.numberValue;
                        if (col.name === "INV DAT" || (num > 30000 && num < 60000)) {
                            val = serialToFormattedDate(num);
                        } else {
                            val = num;
                        }
                    } else if (eff.boolValue !== undefined) {
                        val = eff.boolValue;
                    }
                }
                el.textContent = val;

                // Background Color
                let hasCustomBg = false;
                if (cell && cell.effectiveFormat?.backgroundColor) {
                    const bg = cell.effectiveFormat.backgroundColor;
                    const r = Math.round((bg.red || 0) * 255);
                    const g = Math.round((bg.green || 0) * 255);
                    const b = Math.round((bg.blue || 0) * 255);
                    if (r < 255 || g < 255 || b < 255) {
                        hasCustomBg = true;
                        el.style.backgroundColor = `rgb(${r},${g},${b})`;
                        const brightness = (r * 299 + g * 587 + b * 114) / 1000;
                        if (brightness < 128) el.style.color = 'white';
                    }
                }

                if (!val && !hasCustomBg) {
                    el.classList.add('empty-cell');
                }
                tr.appendChild(el);
            });

            // Action Cell
            const actionTd = document.createElement('td');
            actionTd.className = 'action-cell';
            actionTd.setAttribute('data-label', tab.actionColTitle);

            let invoiceId = '';
            if (invCol && row.values && row.values.length > invCol.index && row.values[invCol.index]) {
                const invCell = row.values[invCol.index];
                invoiceId = invCell.effectiveValue?.stringValue || (invCell.effectiveValue?.numberValue !== undefined ? String(invCell.effectiveValue.numberValue) : '');
            }

            const cellCross = row.values && row.values.length > crossColIdx ? row.values[crossColIdx] : null;
            let existingDateVal = '';
            if (cellCross && cellCross.effectiveValue) {
                if (cellCross.effectiveValue.stringValue) {
                    existingDateVal = cellCross.effectiveValue.stringValue;
                } else if (cellCross.effectiveValue.numberValue !== undefined) {
                    existingDateVal = serialToFormattedDate(cellCross.effectiveValue.numberValue);
                } else if (cellCross.effectiveValue.boolValue !== undefined) {
                    existingDateVal = String(cellCross.effectiveValue.boolValue);
                }
            }

            const container = document.createElement('div');
            container.className = 'd-flex align-items-center justify-content-end w-100 py-1 flex-wrap gap-1';

            // Clear / Delete Container button (Available in Edit Mode for container-bearing sheets)
            if (isEditMode && tab.allowContainerDelete && invoiceId) {
                const deleteBtn = document.createElement('button');
                deleteBtn.className = 'btn btn-xs btn-outline-danger py-1 px-2 me-1 btn-delete-container';
                deleteBtn.title = `Delete container records for ${invoiceId}`;
                deleteBtn.innerHTML = '<i class="fa-solid fa-trash me-1"></i>Clear';
                deleteBtn.addEventListener('click', async (e) => {
                    e.stopPropagation();
                    deleteBtn.disabled = true;
                    deleteBtn.innerHTML = '<i class="fa-solid fa-spinner fa-spin me-1"></i>';
                    try {
                        await deleteContainerRow(invoiceId);
                    } finally {
                        deleteBtn.disabled = false;
                        deleteBtn.innerHTML = '<i class="fa-solid fa-trash me-1"></i>Clear';
                    }
                });
                container.appendChild(deleteBtn);
            }

            if (existingDateVal && existingDateVal.trim() !== '') {
                const badge = document.createElement('span');
                badge.className = 'badge bg-success-subtle text-success border border-success-subtle py-1.5 px-3';
                badge.style.fontSize = '0.8rem';
                badge.style.fontWeight = '600';
                badge.innerHTML = `<i class="fa-solid fa-circle-check me-1"></i>${existingDateVal}`;
                container.appendChild(badge);
            } else {
                const controls = document.createElement('div');
                controls.className = 'd-flex align-items-center gap-2';

                const dateInput = document.createElement('input');
                dateInput.type = 'text';
                dateInput.className = 'form-control form-control-sm border-date-input';
                dateInput.style.width = '110px';
                dateInput.style.padding = '4px 8px';
                dateInput.style.fontSize = '0.85rem';
                dateInput.style.backgroundColor = '#ffffff';

                const commitBtn = document.createElement('button');
                commitBtn.className = 'btn btn-xs btn-primary commit-date-btn py-1.5 px-3';
                commitBtn.style.fontSize = '0.8rem';
                commitBtn.style.whiteSpace = 'nowrap';
                commitBtn.innerHTML = '<i class="fa-solid fa-truck-fast me-sm-1"></i><span class="d-none d-sm-inline">Cross</span>';
                
                commitBtn.addEventListener('click', (e) => {
                    e.stopPropagation();
                    const selectedDate = dateInput.value;
                    if (!selectedDate) {
                        alert("Please select a date first.");
                        return;
                    }
                    
                    const confirmMsg = `Are you sure you want to mark Invoice "${invoiceId}" as crossed on ${selectedDate}?`;
                    if (!confirm(confirmMsg)) {
                        return;
                    }
                    
                    commitCrossBorderDate(tab, invoiceId, row.originalRowIndex, selectedDate, dateInput, commitBtn, actionTd);
                });

                controls.appendChild(dateInput);
                controls.appendChild(commitBtn);
                container.appendChild(controls);

                flatpickr(dateInput, {
                    dateFormat: "d-M-Y",
                    defaultDate: new Date(),
                    allowInput: true
                });
            }

            actionTd.appendChild(container);
            tr.appendChild(actionTd);
            tableBody.appendChild(tr);
        });
    }

    // Call loadSettings on startup
    loadSettings();
});
