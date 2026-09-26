/*
 * 商家页
 *
 * 看板、接单/拒单/出餐、开关店。
 *
 * 每个动作做完都重新拉一遍数据，不在本地猜着改字段：状态机、统计口径
 * 全在服务端，本地改出来的数迟早和它对不上。
 *
 * 哪张卡上冒哪个按钮由 orderCard 决定（待接单给接单/拒单，备餐中给
 * 出餐），真正的拦截在服务端 canTransit 那道 —— 页面少显示一个按钮
 * 只是提前告知，不是安全边界。
 */

let shop   = null;      // 当前这家店（GET /api/shops/:id 的返回）
let stats  = null;      // 看板数字
let orders = [];        // 这家的订单，新的在前（服务端排好的）
let filter = 'all';     // all | todo | closed

function el(id) { return document.getElementById(id); }

/* ---------------- 我是哪家店 ---------------- */

async function loadShopList() {
    const list = await API.get('/api/shops');

    el('shop').innerHTML = list.map(function (m) {
        return '<option value="' + m.id + '">' + esc(m.name) + '</option>';
    }).join('');

    // 上次开的是哪家店，这次还开哪家。来回演示的时候省事。
    const last = recall('merchantId');
    if (last && list.some(function (m) { return String(m.id) === last; }))
        el('shop').value = last;
    if (!el('shop').value && list.length)
        el('shop').value = String(list[0].id);

    await switchTo(parseInt(el('shop').value, 10));
}

async function switchTo(id) {
    if (!id) return;
    remember('merchantId', String(id));
    filter = 'all';
    await reload();
}

// 店和订单一次重拉。订单接口把统计和列表捆在一起给，省得两个数字
// 差半拍（上面统计说 3 单、下面列表只有 2 单）。
async function reload() {
    const id = parseInt(el('shop').value, 10);
    if (!id) return;

    shop = await API.get('/api/shops/' + id);

    const data = await API.get('/api/shops/' + id + '/orders');
    stats  = data.stats;
    orders = data.orders;

    renderShop();
    renderStats();
    renderOrders();
}

/* ---------------- 店头：营业状态和那个开关 ---------------- */

function renderShop() {
    el('shopBox').innerHTML = '<b>' + esc(shop.name) + '</b> ' +
        '<span class="muted">' + esc(shop.location) + ' · 评分 ' + esc(shop.rating) +
        ' · ' + shop.dishCount + ' 道菜</span>';

    el('stateTag').innerHTML = '<span class="tag ' +
        (shop.accepting ? 'tag-open' : 'tag-closed') + '">' + esc(shop.statusText) + '</span>';

    // 按钮上写「按下去会怎样」，不写现在是什么状态 —— 跟门上的牌子一个道理
    el('toggle').textContent = shop.accepting ? '打烊' : '开门营业';
    el('toggle').disabled = false;
}

on(el('toggle'), 'click', async function () {
    const msg = el('msg');
    el('toggle').disabled = true;

    const r = await guard(msg, function () {
        return API.post('/api/shops/' + shop.id + '/accepting', { accepting: !shop.accepting });
    });

    // 这个接口直接把整份商户发回来（没包一层），就地换上
    if (r) {
        shop = r;
        renderShop();
        showMsg(msg, shop.name + ' 现在：' + shop.statusText, true);
    } else {
        el('toggle').disabled = false;    // 失败了得让人能再点一次
    }
});

/* ---------------- 看板 ---------------- */

function renderStats() {
    const cells = [
        ['订单总数', stats.orderCount],
        ['已完成',   stats.done],
        ['进行中',   stats.doing],
        ['已取消',   stats.canceled],
        ['进账',     money(stats.income)]
    ];

    el('stats').innerHTML = cells.map(function (c) {
        return '<div class="stat"><div class="stat-num">' + esc(c[1]) +
               '</div><div class="muted">' + esc(c[0]) + '</div></div>';
    }).join('');
}

/* ---------------- 订单 ---------------- */

// 要商家动手的两种：待接单（接单/拒单）、备餐中（出餐）。
// 出了餐就不归这家店管了，等骑手来取。
function needsMe(o) {
    return o.status === 'placed' || o.status === 'accepted';
}

function isClosed(o) {
    return o.status === 'completed' || o.status === 'canceled';
}

function visibleOrders() {
    if (filter === 'todo')   return orders.filter(needsMe);
    if (filter === 'closed') return orders.filter(isClosed);
    return orders;
}

function renderOrders() {
    renderFilter();

    const list = visibleOrders();
    if (!list.length) {
        const hint = filter === 'todo'   ? '没有要处理的单。'
                   : filter === 'closed' ? '还没有完结的单。'
                   : '这家店还没有订单。';
        el('orders').innerHTML = '<div class="muted">' + hint + '</div>';
        return;
    }

    el('orders').innerHTML = list.map(orderCard).join('');
}

// 三档筛选，按钮上直接标数量 —— 「要处理的」那个数就是这家店现在欠的活
function renderFilter() {
    const chips = [
        ['all',    '全部',     orders.length],
        ['todo',   '要处理的', orders.filter(needsMe).length],
        ['closed', '已完结',   orders.filter(isClosed).length]
    ];

    el('filterBar').innerHTML = chips.map(function (c) {
        const cls = filter === c[0] ? 'btn btn-sm btn-primary' : 'btn btn-sm';
        return '<button class="' + cls + '" data-filter="' + c[0] + '">' +
               c[1] + ' ' + c[2] + '</button>';
    }).join('');
}

on(el('filterBar'), 'click', function (ev) {
    const btn = ev.target.closest('[data-filter]');
    if (!btn) return;
    filter = btn.dataset.filter;
    renderOrders();
});

function orderCard(o) {
    let btns = '';
    if (o.status === 'placed') {
        btns = '<button class="btn btn-sm btn-primary" data-act="accept" data-id="' + o.id + '">接单</button>' +
               '<button class="btn btn-sm btn-danger" data-act="reject" data-id="' + o.id + '">拒单</button>';
    } else if (o.status === 'accepted') {
        btns = '<button class="btn btn-sm btn-primary" data-act="finish" data-id="' + o.id + '">出餐</button>';
    }

    return '<div class="card order">' +
        '<div class="spread">' +
            '<div><b>#' + o.id + '</b> ' + esc(o.customerName) + '</div>' +
            '<div>' + statusTag(o.status) + '</div>' +
        '</div>' +
        '<div class="muted">' + esc(o.itemsSummary) + ' · 共 ' + o.totalQty + ' 份 → ' + esc(o.address) + '</div>' +
        '<div class="spread mt8">' +
            '<div>菜品 ' + money(o.goodsTotal) + ' ' +
                '<span class="muted">客人实付 ' + money(o.total) + ' · ' + esc(o.createdAt) + '</span></div>' +
            '<div class="row">' + btns + '</div>' +
        '</div>' +
        (o.note ? '<div class="muted">备注：' + esc(o.note) + '</div>' : '') +
        (o.riderName ? '<div class="muted">骑手：' + esc(o.riderName) + '</div>' : '') +
        timelineHtml(o) +
    '</div>';
}

on(el('orders'), 'click', async function (ev) {
    const btn = ev.target.closest('[data-act]');
    if (!btn) return;

    const msg = el('msg');
    const id  = btn.dataset.id;
    const act = btn.dataset.act;

    // 拒单要带理由（可以留空，服务端会填一句默认的）；接单和出餐
    // 不需要请求体，发个空对象过去服务端也不看。
    const body = {};
    if (act === 'reject') {
        const reason = prompt('拒单会把钱退回给客人。理由（留空用默认）：', '');
        if (reason === null) return;                // 点了取消，什么都不做
        if (reason.trim() !== '') body.reason = reason.trim();
    }

    btn.disabled = true;
    const r = await guard(msg, function () {
        return API.post('/api/orders/' + id + '/' + act, body);
    });
    if (r) showMsg(msg, '订单 #' + id + ' 现在：' + r.order.statusText, true);

    await reload();
});

/* ---------------- 起跑 ---------------- */

on(el('shop'), 'change', function () { return switchTo(parseInt(el('shop').value, 10)); });

boot(loadShopList);

// 每 5 秒自己重拉一遍：两个人各拿一台设备演示的时候，没人会记得手动刷新，
// 缺了这段就得每换一个角色去按一次 F5。拉不到就闭嘴等下一轮 ——
// 服务端偶尔抽一下不该往页面上弹红字。
setInterval(function () {
    reload().catch(function () {});
}, 5000);