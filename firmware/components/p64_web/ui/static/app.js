// p64 web UI — shared helpers for every page: the API wrapper with the PIN prompt,
// toasts, formatting, the bottom navigation with the update badge, and the status feed
// (WebSocket push with a polling fallback, spec 11.2).
(function () {
    var P = window.p64 = window.p64 || {};

    // ── API ──
    // Every call goes through here. A 401 opens the PIN prompt (the device asks for a
    // PIN and this browser has no session yet); a 429 says the device is locked.
    // A request that hangs (a socket the device purged under a burst) is retried once
    // after 8 s rather than left pending.
    function fetchWithTimeout(path, opt, attempt) {
        var ctrl = ('AbortController' in window) ? new AbortController() : null;
        var o = Object.assign({}, opt || {});
        if (ctrl) o.signal = ctrl.signal;
        var timer = setTimeout(function () { if (ctrl) ctrl.abort(); }, 8000);
        return fetch(path, o).then(function (r) { clearTimeout(timer); return r; }, function (e) {
            clearTimeout(timer);
            if (attempt < 1 && (!opt || !opt.method || opt.method === 'GET')) return fetchWithTimeout(path, opt, attempt + 1);
            throw e;
        });
    }
    P.api = function (path, opt) {
        return fetchWithTimeout(path, opt, 0).then(function (r) {
            if (r.status === 401) { P.showPin(''); throw new Error('PIN required'); }
            if (r.status === 429) { P.showPin('Too many wrong PINs; wait ' + (r.headers.get('Retry-After') || '30') + ' s'); throw new Error('locked'); }
            return r.json().then(function (j) {
                if (!j.ok) throw new Error(j.error || ('HTTP ' + r.status));
                return j.data;
            });
        });
    };
    P.get = function (path) { return P.api(path); };
    P.post = function (path, body) {
        return P.api(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: body ? JSON.stringify(body) : undefined });
    };
    P.put = function (path, body) {
        return P.api(path, { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
    };
    P.del = function (path) { return P.api(path, { method: 'DELETE' }); };
    // Settings: merge a patch; resolves to the whole document.
    P.setting = function (patch, quiet) {
        return P.put('/api/v1/settings', patch).then(function (s) { if (!quiet) P.toast('Saved'); return s; })
            .catch(function (e) { P.toast(e.message, true); throw e; });
    };

    // ── Toast ──
    P.toast = function (msg, isError) {
        var t = document.getElementById('p64-toast');
        if (!t) {
            t = document.createElement('div');
            t.id = 'p64-toast';
            t.className = 'toast';
            document.body.appendChild(t);
        }
        t.textContent = msg;
        t.className = 'toast ' + (isError ? 'toast-error' : 'toast-success') + ' show';
        clearTimeout(t._timer);
        t._timer = setTimeout(function () { t.className = 'toast'; }, isError ? 4000 : 2200);
    };

    // ── Formatting ──
    P.esc = function (s) {
        return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) {
            return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
        });
    };
    P.fmtBytes = function (n) {
        n = +n || 0;
        if (n < 1024) return n + ' B';
        if (n < 1048576) return (n / 1024).toFixed(1) + ' KB';
        if (n < 1073741824) return (n / 1048576).toFixed(1) + ' MB';
        return (n / 1073741824).toFixed(2) + ' GB';
    };
    P.relative = function (seconds) {
        if (seconds == null || seconds < 0) return 'never';
        if (seconds < 60) return 'just now';
        if (seconds < 3600) return Math.floor(seconds / 60) + ' min ago';
        if (seconds < 86400) return Math.floor(seconds / 3600) + ' h ago';
        return Math.floor(seconds / 86400) + ' d ago';
    };
    P.uptime = function (s) {
        s = Math.floor(+s || 0);
        var d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
        return (d ? d + 'd ' : '') + h + 'h ' + m + 'm';
    };
    P.hhmm = function (minutes) {
        minutes = ((+minutes || 0) % 1440 + 1440) % 1440;
        return ('0' + Math.floor(minutes / 60)).slice(-2) + ':' + ('0' + (minutes % 60)).slice(-2);
    };
    P.minutes = function (hhmm) {
        var m = /^(\d{1,2}):(\d{2})$/.exec(hhmm || '');
        return m ? (+m[1] % 24) * 60 + (+m[2] % 60) : 0;
    };
    P.confirmWord = function (question, word) {
        if (!window.confirm(question)) return false;
        if (!word) return true;
        return window.prompt('Type ' + word + ' to confirm') === word;
    };

    // ── PIN prompt (spec 10.3) ──
    P.showPin = function (msg) {
        var o = document.getElementById('p64-pin');
        if (!o) {
            o = document.createElement('div');
            o.id = 'p64-pin';
            o.className = 'pin-overlay-p64';
            o.innerHTML = '<div class="card pin-card-p64"><h3>This p64 asks for its PIN</h3>' +
                '<input type="password" id="p64-pin-entry" class="input" inputmode="numeric" pattern="[0-9]*" maxlength="8" placeholder="4 to 8 digits" autocomplete="current-password">' +
                '<button class="btn btn-full" id="p64-pin-btn">Unlock</button><div class="setting-hint" id="p64-pin-msg"></div></div>';
            document.body.appendChild(o);
            document.getElementById('p64-pin-btn').addEventListener('click', P.login);
            document.getElementById('p64-pin-entry').addEventListener('keydown', function (e) { if (e.key === 'Enter') P.login(); });
        }
        o.style.display = 'flex';
        document.getElementById('p64-pin-msg').textContent = msg || '';
        setTimeout(function () { document.getElementById('p64-pin-entry').focus(); }, 50);
    };
    P.login = function () {
        var pin = document.getElementById('p64-pin-entry').value;
        fetch('/api/v1/auth/login', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ pin: pin }) })
            .then(function (r) { return r.json(); })
            .then(function (j) {
                if (j.ok) { location.reload(); }
                else document.getElementById('p64-pin-msg').textContent = j.error || 'wrong PIN';
            });
    };

    // ── Bottom navigation ──
    P.nav = function (active) {
        var items = [
            ['/', '&#8962;', 'Home', 'home'],
            ['/playsets', '&#9776;', 'Playsets', 'playsets'],
            ['/settings', '&#9881;', 'Settings', 'settings'],
            ['/update', '&#8673;', 'Update', 'update']
        ];
        var html = '';
        items.forEach(function (it) {
            html += '<a href="' + it[0] + '" class="bottom-nav-item' + (it[3] === active ? ' active' : '') + '">' +
                '<span class="bottom-nav-icon">' + it[1] + '</span><span class="bottom-nav-label">' + it[2] + '</span>' +
                (it[3] === 'update' ? '<span class="bottom-nav-badge" id="update-badge"></span>' : '') + '</a>';
        });
        var nav = document.createElement('nav');
        nav.className = 'bottom-nav';
        nav.innerHTML = html;
        document.body.appendChild(nav);
        document.body.classList.add('has-bottom-nav');
    };
    P.updateBadge = function (state, version) {
        var b = document.getElementById('update-badge');
        if (!b) return;
        var on = state === 'available' || state === 'ready_to_reboot';
        b.textContent = on ? '1' : '';
        b.style.display = on ? '' : 'none';
        b.title = version || '';
    };

    // ── Status feed: WebSocket push, 4 s polling fallback (spec 11.2) ──
    var listeners = [], ws = null, pollTimer = null, lastStatus = null;
    P.onStatus = function (fn) { listeners.push(fn); if (lastStatus) fn(lastStatus); };
    P.status = function () { return lastStatus; };
    function deliver(d) {
        lastStatus = d;
        P.updateBadge(d.update_state);
        listeners.forEach(function (fn) { try { fn(d); } catch (e) { console.error(e); } });
    }
    function poll() {
        P.get('/api/v1/status').then(deliver).catch(function () {});
    }
    function connect() {
        if (!('WebSocket' in window)) { poll(); pollTimer = setInterval(poll, 4000); return; }
        try {
            ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/api/v1/ws');
        } catch (e) { poll(); pollTimer = setInterval(poll, 4000); return; }
        var opened = false;
        ws.onopen = function () { opened = true; if (pollTimer) { clearInterval(pollTimer); pollTimer = null; } };
        ws.onmessage = function (ev) {
            try {
                var m = JSON.parse(ev.data);
                if (m.type === 'status' && m.data) deliver(m.data);
            } catch (e) {}
        };
        ws.onclose = ws.onerror = function () {
            ws = null;
            if (!pollTimer) { poll(); pollTimer = setInterval(poll, 4000); }
            setTimeout(connect, opened ? 3000 : 10000);
        };
    }
    P.startStatus = function () { poll(); connect(); };
    P.refresh = poll;

    // ── Auth state on load: shows the prompt at once when a PIN is set ──
    P.checkAuth = function () {
        fetch('/api/v1/auth').then(function (r) { return r.json(); }).then(function (j) {
            var d = j.data || {};
            if (d.pin_set && !d.authenticated) P.showPin('');
        }).catch(function () {});
    };
})();
