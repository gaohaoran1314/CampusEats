/*
 * 学生页
 *
 * 三件事：挑店 -> 点菜 -> 看自己的单。
 *
 * 页面上显示的价钱只是「先看着」：打包费、配送费、满减都在服务端的 Pricing
 * 里，客户端复刻一遍迟早对不上。所以购物车那行写的是「菜品小计」，
 * 真正的实付金额以 POST /api/orders 回的那份为准。
 */

let me      = null;   // 当前这个学生（/api/customers 里的一项）
let curShop = null;   // 正开着的店；菜单和购物车都挂在它下面
let cart    = [];     // [{dishId, name, unitPrice, qty}]
                      // 一次只能有一家店的菜 —— 服务端下一单也只收一个 merchantId

function el(id) { return document.getElementById(id); }

/* ---------------- 我是谁 ---------------- */

async function loadWho() {
    const list = await API.get('/api/customers');

    el('who').innerHTML = list.map(function (c) {
        return '<option value="' + esc(c.studentNo) + '">' +
               esc(c.name) + ' · ' + esc(c.studentNo) + '</option>';
    }).join('');

    // 上次选的是谁就还选谁，省得每次刷新都重挑一遍
    const last = recall('studentNo');
    if (last && list.some(function (c) { return c.studentNo === last; })) {
        el('who').value = last;
    }
    if (!el('who').value && list.length) el('who').value = list[0].studentNo;

    await switchTo(el('who').value);
    await loadShops();
}

async function switchTo(studentNo) {
    if (!studentNo) return;
    el('who').value = studentNo;
    remember('studentNo', studentNo);

    // 换人等于换一张购物车。留着上一个人的菜，下单时会下到别人头上。
    cart = [];
    renderCart();

    await refresh();
    if (me) el('addr').value = me.building;
}

// 余额和订单一起取。服务端那一个请求是在同一把锁里读的，不会出现
// 「余额已经是新的、订单列表还是旧的」。
async function refresh() {
    const no = el('who').value;
    if (!no) return;

    const data = await API.get('/api/orders?studentNo=' + encodeURIComponent(no));
    me = data.customer;

    el('meBox').innerHTML = '<b>' + esc(me.name) + '</b> ' +
        esc(me.studentNo) + ' · ' + esc(me.building);
    el('balance').textContent = money(me.balance);

    renderOrders(data.orders);
}

/* ---------------- 店铺 ---------------- */

async function loadShops() {
    const q = el('q').value.trim();
    const list = await API.get('/api/shops' + (q ? '?q=' + encodeURIComponent(q) : ''));

    if (!list.length) {
        el('shops').innerHTML = '<div class="muted">没有匹配的店。</div>';
        return;
    }

    el('shops').innerHTML = list.map(function (m) {
        const cur = curShop && curShop.id === m.id ? ' active' : '';
        return '<div class="card shop' + cur + '" data-shop="' + m.id + '">' +
            '<div class="spread"><b>' + esc(m.name) + '</b>' +
                '<span class="tag ' + (m.accepting ? 'tag-open' : 'tag-closed') + '">' +
                esc(m.statusText) + '</span></div>' +
            '<div class="muted">' + esc(m.location) + ' · 评分 ' + esc(m.rating) +
                ' · ' + m.dishCount + ' 道菜</div>' +
        '</div>';
    }).join('');
}

async function openShop(id) {
    curShop = await API.get('/api/shops/' + id);
    el('menuCard').hidden = false;
    el('menuTitle').textContent = curShop.name + ' · ' + curShop.statusText;

    el('menu').innerHTML = curShop.menu.length
        ? curShop.menu.map(dishRow).join('')
        : '<div class="muted">这家店还没上菜。</div>';

    renderCart();
    await loadShops();     // 让选中的那张卡片描个边，顺便刷新营业状态
}

function dishRow(d) {
    const off = !d.available;      // 没上架或者没库存，服务端已经算好了
    const dis = off ? ' disabled' : '';
    return '<div class="dish">' +
        '<div class="dish-main">' +
            '<div>' + esc(d.name) +
                (d.onSale ? '' : ' <span class="muted">（已下架）</span>') + '</div>' +
            '<div class="muted">' + esc(d.stockText) + '</div>' +
        '</div>' +
        '<div class="dish-price">' + esc(d.price.text) + '</div>' +
        '<div class="dish-buy">' +
            '<input class="qty" type="number" min="1" max="99" value="1" id="q-' + d.id + '"' + dis + '>' +
            '<button class="btn btn-sm" data-add="' + d.id + '"' + dis + '>加入</button>' +
        '</div>' +
    '</div>';
}

/* ---------------- 购物车 ---------------- */

function addToCart(dishId) {
    const d = curShop.menu.find(function (x) { return x.id === dishId; });
    if (!d) return;

    let qty = parseInt(el('q-' + dishId).value, 10);
    if (!qty || qty < 1) qty = 1;
    if (qty > 99) qty = 99;         // 和服务端那个 1~99 的上限对齐

    const line = cart.find(function (l) { return l.dishId === dishId; });
    if (line) line.qty = Math.min(99, line.qty + qty);
    else      cart.push({ dishId: d.id, name: d.name, unitPrice: d.price, qty: qty });

    renderCart();
}

function renderCart() {
    el('cartCard').hidden = !curShop;
    if (!curShop) return;

    el('cartLines').innerHTML = cart.length
        ? cart.map(function (l, i) {
            return '<div class="spread cart-line">' +
                '<div>' + esc(l.name) + ' × ' + l.qty + '</div>' +
                '<div class="row">' +
                    '<span>' + fen(l.unitPrice.cents * l.qty) + '</span>' +
                    '<button class="btn btn-sm btn-ghost" data-del="' + i + '">去掉</button>' +
                '</div>' +
            '</div>';
        }).join('')
        : '<div class="muted">还没点菜。上面的菜单里填份数，点「加入」。</div>';

    const sum = cart.reduce(function (a, l) { return a + l.unitPrice.cents * l.qty; }, 0);
    el('cartSum').textContent = fen(sum);
}

async function placeOrder() {
    const msg = el('msg');
    if (!me)           { showMsg(msg, '先在上面选一个学生', false); return; }
    if (!cart.length)  { showMsg(msg, '购物车是空的', false); return; }

    const body = {
        studentNo:  me.studentNo,
        merchantId: curShop.id,
        items: cart.map(function (l) { return { dishId: l.dishId, qty: l.qty }; }),
        note: el('note').value.trim()
    };
    const addr = el('addr').value.trim();
    if (addr) body.address = addr;    // 不填就让服务端落到这个学生的宿舍楼

    const r = await guard(msg, function () { return API.post('/api/orders', body); });
    if (!r) return;

    showMsg(msg, '下单成功：' + r.order.itemsSummary + '，实付 ' + money(r.order.total), true);
    cart = [];
    el('note').value = '';

    await openShop(curShop.id);   // 库存被扣了，菜单得重取
    await refresh();              // 余额和订单也跟着变
}

/* ---------------- 我的订单 ---------------- */

function renderOrders(list) {
    if (!list.length) {
        el('orders').innerHTML = '<div class="muted">还没下过单。</div>';
        return;
    }
    el('orders').innerHTML = list.map(orderCard).join('');
}

function orderCard(o) {
    // 能做什么只看状态码，跟服务端的 canTransit 是同一张表。页面少显示一个
    // 按钮只是提前告知 —— 真点错了，服务端照样 409 拦下来。
    let btns = '';
    if (o.status === 'placed' || o.status === 'accepted') {
        btns += '<button class="btn btn-sm btn-danger" data-act="cancel" data-id="' + o.id +
                '">取消订单</button>';
    }
    if (o.status === 'delivering') {
        btns += '<button class="btn btn-sm btn-primary" data-act="receive" data-id="' + o.id +
                '">确认收货</button>';
    }

    let fee = '菜品 ' + money(o.goodsTotal) + ' + 打包 ' + money(o.packFee) +
              ' + 配送 ' + money(o.deliveryFee);
    if (o.discount && o.discount.cents) fee += ' − 优惠 ' + money(o.discount);

    return '<div class="card order">' +
        orderHead(o) +
        '<div class="spread mt8">' +
            '<div>实付 <b>' + money(o.total) + '</b> <span class="muted">（' + fee + '）</span></div>' +
            '<div class="row">' + btns + '</div>' +
        '</div>' +
        (o.note ? '<div class="muted">备注：' + esc(o.note) + '</div>' : '') +
        timelineHtml(o) +
    '</div>';
}

/* ---------------- 绑事件 ---------------- */

on(el('who'), 'change', function () { return switchTo(el('who').value); });
on(el('q'), 'input', loadShops);

on(el('closeMenu'), 'click', function () {
    curShop = null;
    cart = [];
    el('menuCard').hidden = true;
    renderCart();
    return loadShops();
});

on(el('shops'), 'click', function (ev) {
    const card = ev.target.closest('[data-shop]');
    if (!card) return;

    const id = parseInt(card.dataset.shop, 10);
    if (cart.length && curShop && curShop.id !== id) {
        if (!confirm('购物车里还有 ' + curShop.name + ' 的菜，换店会清掉。继续？')) return;
        cart = [];
    }
    return openShop(id);
});

on(el('menu'), 'click', function (ev) {
    const btn = ev.target.closest('[data-add]');
    if (btn) addToCart(parseInt(btn.dataset.add, 10));
});

on(el('cartLines'), 'click', function (ev) {
    const btn = ev.target.closest('[data-del]');
    if (!btn) return;
    cart.splice(parseInt(btn.dataset.del, 10), 1);
    renderCart();
});

on(el('place'), 'click', placeOrder);

on(el('orders'), 'click', async function (ev) {
    const btn = ev.target.closest('[data-act]');
    if (!btn) return;

    const msg = el('msg');
    const id = btn.dataset.id;
    const what = btn.dataset.act;
    if (what === 'cancel' && !confirm('取消订单 #' + id + '？钱会全额退回余额。')) return;

    // 先禁掉，免得手快连点两下发出去两个请求。服务端拦得住第二次，
    // 但让用户看见一句「已经取消了」比看见两句没关系的话强。
    btn.disabled = true;
    const r = await guard(msg, function () {
        return API.post('/api/orders/' + id + '/' + what);
    });
    if (r) showMsg(msg, '订单 #' + id + ' 现在：' + r.order.statusText, true);

    await refresh();
});

on(el('topup'), 'click', async function () {
    const msg = el('msg');
    const amount = parseInt(el('topupYuan').value, 10);
    if (!amount || amount < 1 || amount > 1000) {
        showMsg(msg, '充值金额得是 1~1000 的整数', false);
        return;
    }

    const r = await guard(msg, function () {
        return API.post('/api/customers/' + encodeURIComponent(me.studentNo) + '/topup',
                        { amountYuan: amount });
    });
    if (r) showMsg(msg, '充值成功，余额 ' + money(r.customer.balance), true);

    await refresh();
});

/* ---------------- 起跑 ---------------- */

boot(loadWho);