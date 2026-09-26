/*
 * 骑手页
 *
 * 抢单板（商家出过餐、还没人接的单）+ 我手上的任务 + 战绩。
 *
 * 抢单和送达都要把 riderId 发回服务端。服务端认内部 id 不认工号 ——
 * 存档里两个骑手的工号一模一样（344886608），拿工号根本分不出谁是谁，
 * 所以下拉框里显示的是「名字 · #1021」这种。
 */

let me    = null;   // 当前骑手
let board = [];     // 板上等接的单
let tasks = [];     // 我手上的单
let stats = null;   // 送了多少、挣了多少

function el(id) { return document.getElementById(id); }

/* ---------------- 我是谁 ---------------- */

async function loadWho() {
    const list = await API.get('/api/riders');

    el('who').innerHTML = list.map(function (r) {
        return '<option value="' + r.id + '">' + esc(r.name) + ' · #' + r.id + '</option>';
    }).join('');

    const last = recall('riderId');
    if (last && list.some(function (r) { return String(r.id) === last; }))
        el('who').value = last;
    if (!el('who').value && list.length)
        el('who').value = String(list[0].id);

    await switchTo(parseInt(el('who').value, 10));
}

async function switchTo(riderId) {
    if (!riderId) return;
    remember('riderId', String(riderId));
    await reload();
}

// 板子和任务一起拉。这俩不是一个接口，理论上可能差半拍；但抢单最后
// 能不能成是服务端状态机说了算，页面这里差半拍最多多看几秒旧数据。
async function reload() {
    const id = parseInt(el('who').value, 10);
    if (!id) return;

    const got = await Promise.all([
        API.get('/api/rider/board'),
        API.get('/api/riders/' + id + '/tasks')
    ]);

    board = got[0];
    me    = got[1].rider;
    stats = got[1].stats;
    tasks = got[1].orders;

    renderBoard();
    renderTasks();
    renderStats();
}

/* ---------------- 抢单板 ---------------- */

function renderBoard() {
    if (!board.length) {
        el('board').innerHTML = '<div class="muted">板上没单。商家出餐之后这里会自己冒出来。</div>';
        return;
    }

    el('board').innerHTML = board.map(function (o) {
        return '<div class="card order">' +
            '<div class="spread">' +
                '<div><b>#' + o.id + '</b> ' + esc(o.merchantName) + '</div>' +
                '<div>' + statusTag(o.status) + '</div>' +
            '</div>' +
            '<div class="muted">' + esc(o.itemsSummary) + ' · 共 ' + o.totalQty + ' 份 → ' + esc(o.address) + '</div>' +
            '<div class="spread mt8">' +
                '<div>配送费 <b>' + money(o.deliveryFee) + '</b></div>' +
                '<button class="btn btn-sm btn-primary" data-grab="' + o.id + '">抢这单</button>' +
            '</div>' +
        '</div>';
    }).join('');
}

/* ---------------- 我的任务 ---------------- */

function renderTasks() {
    if (!tasks.length) {
        el('tasks').innerHTML = '<div class="muted">手上没有单。去上面抢一单。</div>';
        return;
    }

    el('tasks').innerHTML = tasks.map(function (o) {
        // 只有「配送中」的需要动手；送完的单留在列表里当记录看，不带按钮
        const btn = o.status === 'delivering'
            ? '<button class="btn btn-sm btn-primary" data-deliver="' + o.id + '">已送达</button>'
            : '';

        return '<div class="card order">' +
            '<div class="spread">' +
                '<div><b>#' + o.id + '</b> ' + esc(o.merchantName) + '</div>' +
                '<div>' + statusTag(o.status) + '</div>' +
            '</div>' +
            '<div class="muted">' + esc(o.itemsSummary) + ' · 共 ' + o.totalQty +
                ' 份 → ' + esc(o.customerName) + '，' + esc(o.address) + '</div>' +
            '<div class="spread mt8">' +
                '<div>配送费 <b>' + money(o.deliveryFee) + '</b> ' +
                    '<span class="muted">' + esc(o.createdAt) + '</span></div>' +
                '<div class="row">' + btn + '</div>' +
            '</div>' +
            timelineHtml(o) +
        '</div>';
    }).join('');
}

/* ---------------- 战绩 ---------------- */

function renderStats() {
    const cells = [
        ['已送单数',   stats.done],
        ['配送费合计', money(stats.fee)]
    ];

    el('stats').innerHTML = cells.map(function (c) {
        return '<div class="stat"><div class="stat-num">' + esc(c[1]) +
               '</div><div class="muted">' + esc(c[0]) + '</div></div>';
    }).join('');
}

/* ---------------- 两个动作 ---------------- */

on(el('board'), 'click', async function (ev) {
    const btn = ev.target.closest('[data-grab]');
    if (!btn) return;

    const msg = el('msg');
    const id  = btn.dataset.grab;

    // 先禁掉再发。手快连点两下的话第二下服务端也会 409 拦下，
    // 但让人先看见「抢到了」再看见「手慢了」太难看。
    btn.disabled = true;

    const r = await guard(msg, function () {
        return API.post('/api/orders/' + id + '/grab', { riderId: me.id });
    });
    if (r) showMsg(msg, '抢到了 #' + id + '：' + r.order.itemsSummary, true);

    await reload();
});

on(el('tasks'), 'click', async function (ev) {
    const btn = ev.target.closest('[data-deliver]');
    if (!btn) return;

    const msg = el('msg');
    const id  = btn.dataset.deliver;
    if (!confirm('#' + id + ' 送到了？点下去这单就完结了。')) return;

    btn.disabled = true;
    const r = await guard(msg, function () {
        return API.post('/api/orders/' + id + '/deliver', { riderId: me.id });
    });
    if (r) showMsg(msg, '订单 #' + id + ' 现在：' + r.order.statusText, true);

    await reload();
});

/* ---------------- 起跑 ---------------- */

on(el('who'), 'change', function () { return switchTo(parseInt(el('who').value, 10)); });

boot(loadWho);

// 跟商家页一样每 5 秒自己刷一遍。这一页本来就是「坐在那儿等活」的，
// 让人手动 F5 太说不过去。
setInterval(function () {
    reload().catch(function () {});
}, 5000);