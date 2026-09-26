/*
 * CampusEats · 三个页面共用的那点东西
 *
 * 请求怎么发、钱怎么显示、状态标签什么颜色、上次选的是谁 —— 这几件事
 * 三个页面都要做。各写一份的话，改个颜色得改三处，迟早漏一处。
 *
 * 用普通的 <script> 引入（不是 type="module"），所以这里定义的东西在
 * 页面自己的 <script> 里直接能用，不用 import。代价是全局多几个名字，
 * 就这三页，值。
 */

/* ---------------- 请求 ---------------- */

async function apiFetch(method, path, body) {
    const opt = { method: method, headers: {} };
    if (body !== undefined) {
        opt.headers['Content-Type'] = 'application/json; charset=utf-8';
        opt.body = JSON.stringify(body);
    }

    const resp = await fetch(path, opt);
    const text = await resp.text();

    // 业务错误一定是 {"error":"..."} 这个形状（服务端所有失败分支都走
    // fail()）；但 404 也可能是 httplib 自己发的一行纯文本 —— 静态文件
    // 没找到就是这样。解析失败不能让整个页面炸掉，所以兜一下。
    let data = null;
    if (text) {
        try { data = JSON.parse(text); }
        catch (e) { data = null; }
    }

    if (!resp.ok) {
        throw new Error((data && data.error) ? data.error : ('HTTP ' + resp.status));
    }
    return data;
}

const API = {
    get:  function (path)       { return apiFetch('GET', path); },
    post: function (path, body) { return apiFetch('POST', path, body === undefined ? {} : body); }
};

/* ---------------- 钱 ---------------- */

// 服务端给钱一律是 {cents, text} 两份。页面上一律用 text ——
// 分转元的四舍五入只该有一处实现，那处在服务端。
function money(m) {
    return m ? m.text : '—';
}

// 把「分」拼成 ¥12.34。只在页面自己估个大概的地方用（购物车小计），
// 凡是服务端算过的金额，一律走 money()。
function fen(cents) {
    const v = Math.abs(cents);
    const s = '¥' + Math.floor(v / 100) + '.' + String(v % 100).padStart(2, '0');
    return cents < 0 ? '-' + s : s;
}

/* ---------------- 状态标签 ---------------- */

// 中文名和 Order.cpp 里 kStatuses 那张表对齐。认码不认字 —— 服务端也发
// statusText，但那句是给日志和报错用的，页面上想上色还是得认码。
const STATUS = {
    placed:     '待接单',
    accepted:   '备餐中',
    ready:      '待取餐',
    delivering: '配送中',
    completed:  '已送达',
    canceled:   '已取消'
};

// class 后面那个引号别漏 —— 漏了的话浏览器会一路吃字符，直到撞上下一个
// 引号：标签里的字根本不显示，后面几个块的样式也跟着丢。上一版就少了它。
function statusTag(code) {
    const key = STATUS[code] ? code : 'completed';   // 认不出来的当灰的显示
    return '<span class="tag tag-' + key + '">' + esc(STATUS[code] || code || '未知') + '</span>';
}

/* ---------------- 转义 ---------------- */

// 菜名、备注、拒单理由都是人填的，直接拼进 innerHTML 就是把 XSS 请进门。
// 凡是要拼 HTML 的地方，只要那段内容来自数据，先过一遍这个。
//
// & 必须排在最前面：先换 & 再换别的。顺序反了的话，刚生成的 &lt; 里那个
// & 会被再转一遍，页面上就真的显示出 "&lt;" 这几个字符。
function esc(s) {
    return String(s === null || s === undefined ? '' : s)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

/* ---------------- 记住「我是谁」 ---------------- */

// 每刷新一次就重选一遍身份太烦，存 localStorage 里。
// 只是省事用的，不作为任何身份凭据 —— 服务端本来也不认身份。
function remember(key, val) {
    if (val) localStorage.setItem('campus.' + key, val);
    else     localStorage.removeItem('campus.' + key);
}

function recall(key) {
    return localStorage.getItem('campus.' + key) || '';
}

/* ---------------- 页面零件 ---------------- */

// 统一的消息条。约定：每个页面都放一个 <div id="msg" class="msg" hidden></div>。
function showMsg(box, text, ok) {
    if (!box) return;
    if (!text) { box.hidden = true; box.textContent = ''; return; }
    box.hidden = false;
    box.className = 'msg ' + (ok ? 'msg-ok' : 'msg-bad');
    box.textContent = text;
}

// 所有按钮的点击都是这个套路：错了就把服务端那句话原样显示出来。
// 服务端的报错本来就是写给用户看的（「余额不够，还差 ¥4.00」「手慢了，
// 这单已经被抢走了」），再包一层「操作失败」反而把有用的话盖掉了。
async function guard(box, fn) {
    try {
        return await fn();
    } catch (e) {
        showMsg(box, e.message, false);
        return null;
    }
}

// 绑事件时顺手把 Promise 的异常接住，否则出错只会变成控制台里一条
// 没人看的 Unhandled Rejection，页面上什么都不显示。
function on(target, type, fn) {
    target.addEventListener(type, function (ev) {
        const r = fn(ev);
        if (r && typeof r.catch === 'function') {
            r.catch(function (e) {
                showMsg(document.getElementById('msg'), e.message, false);
            });
        }
    });
}

// 页面开头的载入。失败通常意味着服务端没起来或者接口改了名，这种时候
// 页面上得留句话，别让人对着一片空白发呆。
function boot(fn) {
    Promise.resolve().then(fn).catch(function (e) {
        showMsg(document.getElementById('msg'), '载入失败：' + e.message, false);
    });
}

/* ---------------- 订单的两块公共零件 ---------------- */

function orderHead(o) {
    return '<div class="spread">' +
            '<div><b>#' + o.id + '</b> ' + esc(o.merchantName) + '</div>' +
            '<div>' + statusTag(o.status) + '</div>' +
        '</div>' +
        '<div class="muted">' + esc(o.itemsSummary) + ' · 共 ' + o.totalQty + ' 份</div>' +
        '<div class="muted">送到 ' + esc(o.address) + '</div>';
}

// 时间线按发生顺序给，照抄就行。actor 是自由文本（"学生" / "商家接单" /
// 骑手名 / "学生确认收货"），别去解析它，显示出来就够了。
function timelineHtml(o) {
    if (!o.timeline || !o.timeline.length) return '';
    return '<ul class="tl">' + o.timeline.map(function (e) {
        return '<li>' + esc(e.at) + ' · ' + esc(e.statusText) +
               '（' + esc(e.actor) + '）</li>';
    }).join('') + '</ul>';
}