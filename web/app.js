/* ---------------------------------------------------------------------------
   Wayfinder — browser front-end for the C++ journey planner.

   The UI never reimplements the search: every answer, layer count and timing
   comes from POST /api/journey, which calls find_journey_stats() in
   src/graph.cpp. The client only parses input text and draws the result.
--------------------------------------------------------------------------- */
'use strict';

const SVG_NS = 'http://www.w3.org/2000/svg';

const PALETTE = [
  '#5eead4', '#38bdf8', '#a78bfa', '#fbbf24', '#fb7185', '#34d399',
  '#f472b6', '#22d3ee', '#facc15', '#818cf8', '#4ade80', '#fb923c',
];

const DEPTH_COLORS = [
  '#5eead4', '#38bdf8', '#a78bfa', '#fbbf24', '#fb7185', '#f472b6',
  '#34d399', '#22d3ee',
];

const SAMPLES = [
  {
    file: 'city.txt',
    title: 'Bengaluru demo',
    note: 'Seven named routes across real places. Close V1 and Whitefield is stranded.',
    badge: '7 routes',
  },
  {
    file: 'demo.txt',
    title: 'Buses ≠ stops',
    note: 'Two buses over ten stops beat three buses over four. The viva case.',
    badge: '5 routes',
  },
  {
    file: 'example1.txt',
    title: 'Problem statement',
    note: 'R1 then R2, change at 7. Closing R2 removes every journey.',
    badge: '2 routes',
  },
  {
    file: 'city-large.txt',
    title: 'Scale test',
    note: '400 routes, 2,000 stops, 34,903 appearances, 100 queries.',
    badge: '400 routes',
  },
];

const state = {
  network: null,
  sampleFile: null,
  routeByName: new Map(),
  stopRoutes: new Map(),
  stopSet: new Set(),
  colorFor: new Map(),
  stops: [],
  positions: new Map(),
  layerPositions: new Map(),
  bounds: null,
  layerBounds: null,
  mode: 'network',
  view: { x: 0, y: 0, k: 1 },
  closed: new Set(),
  result: null,
  impact: null,
  log: [],
  queries: [],
  viewport: null,
  routeLayer: null,
  routePaths: new Map(),
  hoveredRoute: null,
  journeyPaths: [],
  raf: 0,
  drag: null,
  dragged: false,
  queryToken: 0,
  pendingQuery: false,
  activeField: 'from',
  busy: false,
};

const $ = (selector) => document.querySelector(selector);

// --------------------------------------------------------------- utilities

function elem(tag, className, text) {
  const node = document.createElement(tag);
  if (className) {
    node.className = className;
  }
  if (text !== undefined) {
    node.textContent = text;
  }
  return node;
}

function svgEl(tag, attrs) {
  const node = document.createElementNS(SVG_NS, tag);
  if (attrs) {
    for (const key of Object.keys(attrs)) {
      node.setAttribute(key, attrs[key]);
    }
  }
  return node;
}

function clear(node) {
  while (node.firstChild) {
    node.removeChild(node.firstChild);
  }
}

function debounce(fn, delay) {
  let timer = 0;
  return (...args) => {
    clearTimeout(timer);
    timer = setTimeout(() => fn(...args), delay);
  };
}

function formatMicros(micros) {
  if (micros < 1000) {
    return `${micros} µs`;
  }
  return `${(micros / 1000).toFixed(1)} ms`;
}

function formatCount(value) {
  return new Intl.NumberFormat('en-IN').format(value);
}

async function copyText(text) {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch (error) {
    const area = document.createElement('textarea');
    area.value = text;
    area.style.position = 'fixed';
    area.style.opacity = '0';
    document.body.appendChild(area);
    area.select();
    let ok = false;
    try {
      ok = document.execCommand('copy');
    } catch (ignored) {
      ok = false;
    }
    area.remove();
    return ok;
  }
}

let toastTimer = 0;
function toast(message) {
  const node = $('#toast');
  node.textContent = message;
  node.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => {
    node.hidden = true;
  }, 2200);
}

async function api(path, body) {
  const options = body
    ? {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
      }
    : {};
  const response = await fetch(path, options);
  let data = null;
  try {
    data = await response.json();
  } catch (error) {
    throw new Error(`Server returned ${response.status}`);
  }
  if (!response.ok) {
    throw new Error(data && data.error ? data.error : `HTTP ${response.status}`);
  }
  return data;
}

// --------------------------------------------------------- network parsing

// Mirrors docs/CONTRACT.md section 2, but rejects nothing silently: the first
// bad line is reported with its line number, so a pasted network is easy to fix.
function parseNetworkText(text) {
  const lines = [];
  text.split(/\r?\n/).forEach((line, index) => {
    const trimmed = line.trim();
    if (!trimmed || trimmed.startsWith('#')) {
      return;
    }
    lines.push({ tokens: trimmed.split(/\s+/), number: index + 1 });
  });

  if (lines.length === 0) {
    throw new Error('The input is empty.');
  }
  const header = lines[0];
  if (header.tokens.length !== 2 || header.tokens[0] !== 'routes') {
    throw new Error(`Line ${header.number}: expected "routes <n>".`);
  }
  if (!/^\d+$/.test(header.tokens[1])) {
    throw new Error(`Line ${header.number}: route count must be a whole number.`);
  }
  const routeCount = Number(header.tokens[1]);
  if (routeCount > 400) {
    throw new Error('At most 400 routes are allowed.');
  }

  const routes = [];
  const seen = new Set();
  let appearances = 0;

  for (let i = 0; i < routeCount; i += 1) {
    const item = lines[1 + i];
    if (!item) {
      throw new Error(`Expected ${routeCount} routes, found ${i}.`);
    }
    const tokens = item.tokens;
    if (tokens.length < 2) {
      throw new Error(`Line ${item.number}: expected "<route> <k> <stops…>".`);
    }
    const name = tokens[0];
    if (seen.has(name)) {
      throw new Error(`Line ${item.number}: duplicate route "${name}".`);
    }
    if (!/^\d+$/.test(tokens[1])) {
      throw new Error(`Line ${item.number}: stop count must be a whole number.`);
    }
    const stopCount = Number(tokens[1]);
    const stops = tokens.slice(2);
    if (stops.length !== stopCount) {
      throw new Error(
        `Line ${item.number}: route "${name}" declares ${stopCount} stops, got ${stops.length}.`
      );
    }
    appearances += stopCount;
    if (appearances > 40000) {
      throw new Error('At most 40,000 stop appearances are allowed.');
    }
    seen.add(name);
    routes.push({ name, stops });
  }

  // Query lines are optional; the UI offers them as demo pairs.
  const queries = [];
  for (let i = 1 + routeCount; i < lines.length; i += 1) {
    const tokens = lines[i].tokens;
    if (tokens[0] !== 'query' || tokens.length < 3) {
      continue;
    }
    queries.push({
      source: tokens[1],
      destination: tokens[2],
      inactive: tokens.slice(3),
    });
  }

  return { routes, queries };
}

// ------------------------------------------------------------------ state

function indexNetwork() {
  state.routeByName.clear();
  state.stopRoutes.clear();
  state.stopSet.clear();
  state.colorFor.clear();
  state.stops = [];

  state.network.routes.forEach((route, index) => {
    state.routeByName.set(route.name, route);
    state.colorFor.set(route.name, PALETTE[index % PALETTE.length]);
    for (const stop of route.stops) {
      if (!state.stopRoutes.has(stop)) {
        state.stopRoutes.set(stop, new Set());
        state.stops.push(stop);
      }
      state.stopRoutes.get(stop).add(route.name);
      state.stopSet.add(stop);
    }
  });
}

function colorFor(routeName) {
  return state.colorFor.get(routeName) || '#8494ad';
}

// ----------------------------------------------------------------- layout

function goldenStart(count) {
  // Deterministic spiral start: same picture on every reload, no RNG.
  return Array.from({ length: count }, (unused, i) => {
    const radius = 24 + Math.sqrt(i) * 34;
    const angle = i * 2.399963229728653;
    return { x: Math.cos(angle) * radius, y: Math.sin(angle) * radius };
  });
}

// Force-directed layout for small networks: routes pull their consecutive
// stops together, every pair pushes apart, gravity keeps the map on screen.
function forceLayout() {
  const stops = state.stops;
  const count = stops.length;
  if (count === 0) {
    state.positions = new Map();
    return;
  }
  if (count > 220) {
    // A force layout of thousands of nodes is a hairball. Sort by name and lay
    // the stops on a grid: numeric networks become an orderly wafer of route
    // lines, which reads far better than a spiral of random chords.
    const ordered = stops.slice().sort((a, b) =>
      a.localeCompare(b, undefined, { numeric: true })
    );
    const columns = Math.ceil(Math.sqrt(count * 1.4));
    const gap = 22;
    state.positions = new Map();
    ordered.forEach((stop, i) => {
      state.positions.set(stop, {
        x: (i % columns) * gap,
        y: Math.floor(i / columns) * gap,
      });
    });
    return;
  }

  const index = new Map(stops.map((stop, i) => [stop, i]));
  const pos = goldenStart(count);
  const edges = [];
  const seen = new Set();

  for (const route of state.network.routes) {
    for (let i = 0; i + 1 < route.stops.length; i += 1) {
      const a = index.get(route.stops[i]);
      const b = index.get(route.stops[i + 1]);
      if (a === undefined || b === undefined || a === b) {
        continue;
      }
      const key = a < b ? `${a}:${b}` : `${b}:${a}`;
      if (!seen.has(key)) {
        seen.add(key);
        edges.push([a, b]);
      }
    }
  }

  const preferred = 38;
  let temperature = 16;

  for (let iteration = 0; iteration < 380; iteration += 1) {
    const disp = new Float64Array(count * 2);

    for (let i = 0; i < count; i += 1) {
      for (let j = i + 1; j < count; j += 1) {
        let dx = pos[i].x - pos[j].x;
        let dy = pos[i].y - pos[j].y;
        let d2 = dx * dx + dy * dy;
        if (d2 < 0.01) {
          dx = (i - j) * 0.01 + 0.001;
          dy = 0.002;
          d2 = dx * dx + dy * dy;
        }
        const distance = Math.sqrt(d2);
        const force = (preferred * preferred) / distance;
        const fx = (dx / distance) * force;
        const fy = (dy / distance) * force;
        disp[2 * i] += fx;
        disp[2 * i + 1] += fy;
        disp[2 * j] -= fx;
        disp[2 * j + 1] -= fy;
      }
    }

    for (const [a, b] of edges) {
      const dx = pos[a].x - pos[b].x;
      const dy = pos[a].y - pos[b].y;
      const distance = Math.sqrt(dx * dx + dy * dy) || 0.001;
      const force = ((distance - preferred) / preferred) * 0.45;
      const fx = (dx / distance) * force;
      const fy = (dy / distance) * force;
      disp[2 * a] -= fx;
      disp[2 * a + 1] -= fy;
      disp[2 * b] += fx;
      disp[2 * b + 1] += fy;
    }

    for (let i = 0; i < count; i += 1) {
      disp[2 * i] -= pos[i].x * 0.015;
      disp[2 * i + 1] -= pos[i].y * 0.015;

      const dx = disp[2 * i];
      const dy = disp[2 * i + 1];
      const length = Math.hypot(dx, dy);
      if (length > 0.0001) {
        const step = Math.min(length, temperature) / length;
        pos[i].x += dx * step;
        pos[i].y += dy * step;
      }
    }
    temperature *= 0.985;
  }

  state.positions = new Map(
    stops.map((stop, i) => [stop, { x: pos[i].x, y: pos[i].y }])
  );
}

function boundsOf(positions) {
  let minX = Infinity;
  let minY = Infinity;
  let maxX = -Infinity;
  let maxY = -Infinity;
  for (const point of positions.values()) {
    minX = Math.min(minX, point.x);
    minY = Math.min(minY, point.y);
    maxX = Math.max(maxX, point.x);
    maxY = Math.max(maxY, point.y);
  }
  if (!Number.isFinite(minX)) {
    return { minX: -1, minY: -1, maxX: 1, maxY: 1 };
  }
  if (maxX - minX < 1) {
    maxX = minX + 1;
  }
  if (maxY - minY < 1) {
    maxY = minY + 1;
  }
  return { minX, minY, maxX, maxY };
}

// World coordinates are normalised to the visible canvas at scale 1. Without
// this, a fitted view shrinks the SVG group to ~0.45 and every 10px label
// becomes 4px and illegible. Pan/zoom then works from a sensible baseline.
function fitWorldToStage(positions, padding) {
  if (!positions || positions.size === 0) {
    return;
  }
  const wrap = $('#mapWrap');
  const width = Math.max(320, wrap.clientWidth);
  const height = Math.max(240, wrap.clientHeight);
  const bounds = boundsOf(positions);
  const spanX = Math.max(1, bounds.maxX - bounds.minX);
  const spanY = Math.max(1, bounds.maxY - bounds.minY);
  const pad = padding === undefined ? 70 : padding;
  const scale = Math.min(
    (width - pad * 2) / spanX,
    (height - pad * 2) / spanY,
    2.2
  );
  // A wide stage suits a slightly wider graph; a mild stretch fills the canvas
  // without turning the map into a funhouse mirror.
  const stageAspect = width / height;
  const contentAspect = spanX / spanY;
  const stretchX =
    contentAspect < stageAspect
      ? Math.min(1.35, stageAspect / contentAspect)
      : 1;
  const cx = (bounds.minX + bounds.maxX) / 2;
  const cy = (bounds.minY + bounds.maxY) / 2;
  for (const point of positions.values()) {
    point.x = (point.x - cx) * scale * stretchX;
    point.y = (point.y - cy) * scale;
  }
}

function layoutNetwork() {
  forceLayout();
  fitWorldToStage(state.positions, 72);
  state.bounds = boundsOf(state.positions);
}

// ------------------------------------------------------------------- zoom

function applyView() {
  if (!state.viewport) {
    return;
  }
  state.viewport.setAttribute(
    'transform',
    `translate(${state.view.x} ${state.view.y}) scale(${state.view.k})`
  );
}

function fitView() {
  const wrap = $('#mapWrap');
  const width = Math.max(320, wrap.clientWidth);
  const height = Math.max(240, wrap.clientHeight);
  const bounds = state.mode === 'layers' ? state.layerBounds : state.bounds;
  if (!bounds) {
    return;
  }
  const spanX = Math.max(1, bounds.maxX - bounds.minX);
  const spanY = Math.max(1, bounds.maxY - bounds.minY);
  const padding = state.mode === 'layers' ? 54 : 64;
  const scale = Math.min(
    (width - padding * 2) / spanX,
    (height - padding * 2) / spanY,
    state.mode === 'layers' ? 1.5 : 1.8
  );
  const k = Math.max(0.12, scale);
  const cx = (bounds.minX + bounds.maxX) / 2;
  const cy = (bounds.minY + bounds.maxY) / 2;
  state.view = { k, x: width / 2 - cx * k, y: height / 2 - cy * k };
  applyView();
}

function zoomBy(factor) {
  const wrap = $('#mapWrap');
  const width = wrap.clientWidth;
  const height = wrap.clientHeight;
  const next = Math.min(6, Math.max(0.15, state.view.k * factor));
  const ratio = next / state.view.k;
  state.view.x = width / 2 - (width / 2 - state.view.x) * ratio;
  state.view.y = height / 2 - (height / 2 - state.view.y) * ratio;
  state.view.k = next;
  applyView();
}

// --------------------------------------------------------------- map draw

function nodeRadius(stop) {
  const total = state.stopSet.size;
  if (total > 800) {
    return 1.7;
  }
  if (total > 240) {
    return 2.5;
  }
  const degree = (state.stopRoutes.get(stop) || new Set()).size;
  return 3.4 + Math.min(3.4, (degree - 1) * 0.9);
}

function stopPoint(stop, positions) {
  return positions.get(stop) || null;
}

function legPoints(leg, positions) {
  const route = state.routeByName.get(leg.route);
  const from = positions.get(leg.from);
  const to = positions.get(leg.to);
  if (!route || !from || !to) {
    return from && to ? [from, to] : [];
  }
  const i = route.stops.indexOf(leg.from);
  const j = route.stops.indexOf(leg.to);
  if (i < 0 || j < 0) {
    return [from, to];
  }
  const low = Math.min(i, j);
  const high = Math.max(i, j);
  let slice = route.stops.slice(low, high + 1);
  if (i > j) {
    slice = slice.slice().reverse();
  }
  const points = [];
  for (const stop of slice) {
    const point = positions.get(stop);
    if (point) {
      points.push(point);
    }
  }
  return points.length >= 2 ? points : [from, to];
}

function pathFrom(points) {
  if (points.length === 0) {
    return '';
  }
  let data = `M ${points[0].x.toFixed(2)} ${points[0].y.toFixed(2)}`;
  for (let i = 1; i < points.length; i += 1) {
    data += ` L ${points[i].x.toFixed(2)} ${points[i].y.toFixed(2)}`;
  }
  return data;
}

function makeTooltip(stop) {
  return (event) => {
    const tip = $('#tooltip');
    clear(tip);
    tip.appendChild(elem('b', null, stop));
    const routes = Array.from(state.stopRoutes.get(stop) || []);
    const closedCount = routes.filter((name) => state.closed.has(name)).length;
    let label = `${routes.length} route${routes.length === 1 ? '' : 's'}`;
    if (closedCount > 0) {
      label += ` · ${closedCount} closed`;
    }
    tip.appendChild(elem('div', 'tt-routes', label));
    if (state.mode === 'layers' && state.layerDepthByStop) {
      const depth = state.layerDepthByStop.get(stop);
      if (depth !== undefined) {
        tip.appendChild(
          elem(
            'div',
            'tt-routes',
            depth === 0 ? 'source · 0 buses' : `${depth} bus${depth === 1 ? '' : 'es'} from source`
          )
        );
      }
    }
    const rect = $('#mapWrap').getBoundingClientRect();
    tip.style.left = `${event.clientX - rect.left}px`;
    tip.style.top = `${event.clientY - rect.top}px`;
    tip.hidden = false;
  };
}

function hideTooltip() {
  $('#tooltip').hidden = true;
}

function pickStop(stop) {
  if (state.dragged) {
    return;
  }
  const fromInput = $('#fromInput');
  const toInput = $('#toInput');
  if (state.activeField === 'from') {
    fromInput.value = stop;
    state.activeField = 'to';
  } else {
    toInput.value = stop;
    state.activeField = 'from';
  }
  syncClearButtons();
  if (fromInput.value.trim() && toInput.value.trim()) {
    runQuery();
  }
}

function drawNetworkMap(viewport, width, height) {
  const large = state.stopSet.size > 380;
  const routeLayer = svgEl('g', { class: 'route-layer' });
  const journeyLayer = svgEl('g');
  const stopLayer = svgEl('g');
  const labelLayer = svgEl('g');
  state.routeLayer = routeLayer;
  state.routePaths = new Map();

  for (const route of state.network.routes) {
    const points = [];
    for (const stop of route.stops) {
      const point = state.positions.get(stop);
      if (point) {
        points.push(point);
      }
    }
    if (points.length === 0) {
      continue;
    }
    const closed = state.closed.has(route.name);
    // Dense networks get one muted hue: 400 colours are noise, and the
    // journey overlay is the only thing that should compete for attention.
    const stroke = closed
      ? '#475569'
      : large
        ? '#2f6273'
        : colorFor(route.name);
    const path = svgEl('path', {
      d: pathFrom(points),
      class: closed ? 'route-path closed' : 'route-path',
      'data-route': route.name,
      stroke,
      'stroke-width': large ? 0.9 : points.length <= 2 ? 2 : 2.4,
      opacity: closed ? 0.35 : large ? 0.42 : 0.44,
    });
    path.dataset.baseStroke = stroke;
    state.routePaths.set(route.name, path);
    routeLayer.appendChild(path);
  }
  applyHoveredRoute();

  // Journey overlay: drawn under the stop nodes, with a glow under the core.
  const legs = state.result && state.result.found ? state.result.legs : [];
  state.journeyPaths = [];
  for (const leg of legs) {
    const points = legPoints(leg, state.positions);
    if (points.length < 2) {
      continue;
    }
    const color = colorFor(leg.route);
    const glow = svgEl('path', {
      d: pathFrom(points),
      class: 'journey-glow',
      stroke: color,
      'stroke-width': 11,
    });
    const line = svgEl('path', {
      d: pathFrom(points),
      class: 'journey-line',
      stroke: color,
      'stroke-width': large ? 3.4 : 4.2,
    });
    journeyLayer.appendChild(glow);
    journeyLayer.appendChild(line);
    state.journeyPaths.push(line);
  }

  const source = state.result ? state.result.source : '';
  const destination = state.result ? state.result.destination : '';
  const transfers = new Set(state.result ? state.result.transfers : []);
  const journeyStops = new Set();
  for (const leg of legs) {
    journeyStops.add(leg.from);
    journeyStops.add(leg.to);
  }

  for (const stop of state.stops) {
    const point = state.positions.get(stop);
    if (!point) {
      continue;
    }
    const radius = nodeRadius(stop);
    const onJourney = journeyStops.has(stop);
    const node = svgEl('circle', {
      cx: point.x.toFixed(2),
      cy: point.y.toFixed(2),
      r: onJourney ? radius + 1.1 : radius,
      class: 'stop-node',
      fill: onJourney ? colorFor((state.result.legs.find((leg) => leg.from === stop || leg.to === stop) || {}).route) : '#0b1424',
      stroke: onJourney ? '#06121a' : 'rgba(148, 163, 184, 0.45)',
      'stroke-width': onJourney ? 1.2 : large ? 0.5 : 1,
    });
    if (!onJourney) {
      node.setAttribute('fill', '#0b1424');
    }
    node.addEventListener('mouseenter', makeTooltip(stop));
    node.addEventListener('mouseleave', hideTooltip);
    node.addEventListener('click', (event) => {
      event.stopPropagation();
      pickStop(stop);
    });
    stopLayer.appendChild(node);
  }

  // Endpoint rings sit above the plain nodes.
  if (source && state.positions.has(source)) {
    const point = state.positions.get(source);
    stopLayer.appendChild(
      svgEl('circle', {
        cx: point.x,
        cy: point.y,
        r: nodeRadius(source) + 5,
        fill: 'none',
        stroke: '#5eead4',
        'stroke-width': 1.4,
        opacity: 0.8,
      })
    );
  }
  if (destination && state.positions.has(destination)) {
    const point = state.positions.get(destination);
    stopLayer.appendChild(
      svgEl('circle', {
        cx: point.x,
        cy: point.y,
        r: nodeRadius(destination) + 5,
        fill: 'none',
        stroke: '#fbbf24',
        'stroke-width': 1.4,
        opacity: 0.9,
      })
    );
  }
  for (const stop of transfers) {
    const point = state.positions.get(stop);
    if (!point) {
      continue;
    }
    stopLayer.appendChild(
      svgEl('circle', {
        cx: point.x,
        cy: point.y,
        r: nodeRadius(stop) + 6,
        class: 'transfer-ring',
      })
    );
  }

  // Labels: all stops on small maps, only meaningful ones when dense. Labels
  // are placed most-important first and skipped if they would collide, which
  // keeps dense interchanges readable.
  const showAll = state.stops.length <= 90;
  const dense = state.stops.length > 240;
  const importance = (stop) =>
    (stop === source ? 100 : 0) +
    (stop === destination ? 100 : 0) +
    (transfers.has(stop) ? 50 : 0) +
    (dense ? 0 : (state.stopRoutes.get(stop) || new Set()).size);
  const ordered = state.stops.slice().sort((a, b) => importance(b) - importance(a));
  const placedLabels = [];
  for (const stop of ordered) {
    const point = state.positions.get(stop);
    if (!point) {
      continue;
    }
    const important =
      stop === source ||
      stop === destination ||
      transfers.has(stop) ||
      (!dense && (state.stopRoutes.get(stop) || new Set()).size >= 3);
    if (!showAll && !important) {
      continue;
    }
    const boxWidth = stop.length * 5.8 + 10;
    const boxHeight = 13;
    const box = {
      minX: point.x - boxWidth / 2,
      maxX: point.x + boxWidth / 2,
      minY: point.y - nodeRadius(stop) - 6 - boxHeight,
      maxY: point.y - nodeRadius(stop) - 6,
    };
    const collides = placedLabels.some(
      (other) =>
        box.minX < other.maxX &&
        box.maxX > other.minX &&
        box.minY < other.maxY &&
        box.maxY > other.minY
    );
    if (collides && !important) {
      continue;
    }
    placedLabels.push(box);
    const label = svgEl('text', {
      x: point.x,
      y: point.y - nodeRadius(stop) - 6,
      'text-anchor': 'middle',
      class: important ? 'stop-label strong' : 'stop-label',
    });
    label.textContent = stop;
    labelLayer.appendChild(label);
  }

  viewport.appendChild(routeLayer);
  viewport.appendChild(journeyLayer);
  viewport.appendChild(stopLayer);
  viewport.appendChild(labelLayer);
  return { width, height };
}

// BFS layer view: source at the centre, one ring per bus, edges reconstructed
// from the routes the search actually expanded.
function layoutLayers() {
  const result = state.result;
  if (!result || !result.found && result.stats.reachable.length === 0) {
    state.layerPositions = new Map();
    state.layerBounds = null;
    return;
  }

  const reachable = result.stats.reachable;
  const depthByStop = new Map();
  const byDepth = new Map();
  for (const item of reachable) {
    depthByStop.set(item.stop, item.buses);
    if (!byDepth.has(item.buses)) {
      byDepth.set(item.buses, []);
    }
    byDepth.get(item.buses).push(item.stop);
  }

  const maxDepth = Math.max(...byDepth.keys(), 0);
  const gap = 76;
  const radiusFor = (depth) => (depth === 0 ? 0 : 62 + (depth - 1) * gap);
  const positions = new Map();
  const angleByStop = new Map();

  for (let depth = 0; depth <= maxDepth; depth += 1) {
    const stops = (byDepth.get(depth) || []).slice();
    stops.sort((a, b) => a.localeCompare(b, undefined, { numeric: true }));
    const count = stops.length;
    const radius = radiusFor(depth);
    const offset = depth * 0.7 - Math.PI / 2;
    stops.forEach((stop, index) => {
      const angle = count === 1 ? offset : offset + (index / count) * Math.PI * 2;
      positions.set(stop, {
        x: Math.cos(angle) * radius,
        y: Math.sin(angle) * radius,
      });
      angleByStop.set(stop, angle);
    });
  }

  // Parent edges: a route expanded at depth d-1 is a valid way into a stop at
  // depth d; choose the previous-layer stop closest in angle for a clean tree.
  const routeDepth = new Map();
  const expandedBuses = result.stats.expanded_route_buses || [];
  result.stats.expanded_routes.forEach((name, index) => {
    routeDepth.set(name, expandedBuses[index]);
  });

  const edges = [];
  for (const [stop, depth] of depthByStop) {
    if (depth === 0) {
      continue;
    }
    const angle = angleByStop.get(stop);
    let best = null;
    let bestDelta = Infinity;
    for (const routeName of state.stopRoutes.get(stop) || []) {
      if (routeDepth.get(routeName) !== depth - 1) {
        continue;
      }
      const route = state.routeByName.get(routeName);
      if (!route) {
        continue;
      }
      for (const candidate of route.stops) {
        if (depthByStop.get(candidate) !== depth - 1) {
          continue;
        }
        const candidateAngle = angleByStop.get(candidate);
        if (candidateAngle === undefined) {
          continue;
        }
        let delta = Math.abs(candidateAngle - angle);
        delta = Math.min(delta, Math.PI * 2 - delta);
        if (delta < bestDelta) {
          bestDelta = delta;
          best = candidate;
        }
      }
    }
    if (best) {
      edges.push([best, stop]);
    }
  }

  state.layerPositions = positions;
  fitWorldToStage(positions, 64);
  state.layerBounds = boundsOf(positions);
  state.layerEdges = edges;
  state.layerDepthByStop = depthByStop;
}

function drawLayerMap(viewport) {
  const positions = state.layerPositions;
  const result = state.result;
  if (!positions || positions.size === 0) {
    return;
  }

  const depthLayer = svgEl('g');
  const edgeLayer = svgEl('g');

  // Radius per depth, measured from the placed nodes (the layout is normalised
  // to the canvas after it is built).
  const radiusByDepth = new Map();
  for (const [stop, point] of positions) {
    const depth = state.layerDepthByStop.get(stop) || 0;
    const radius = Math.hypot(point.x, point.y);
    radiusByDepth.set(depth, Math.max(radiusByDepth.get(depth) || 0, radius));
  }
  const depths = new Map();
  for (const [stop] of positions) {
    const depth = state.layerDepthByStop.get(stop) || 0;
    if (!depths.has(depth)) {
      depths.set(depth, []);
    }
    depths.get(depth).push(stop);
  }
  const maxDepth = Math.max(...depths.keys(), 0);

  // Concentric guides: one ring per bus. Without these the radial layout is
  // only implied by distance.
  for (let depth = 1; depth <= maxDepth; depth += 1) {
    const radius = radiusByDepth.get(depth) || 0;
    if (radius > 0) {
      depthLayer.appendChild(
        svgEl('circle', {
          cx: 0,
          cy: 0,
          r: radius,
          fill: 'none',
          stroke: 'rgba(148, 163, 184, 0.28)',
          'stroke-width': 1,
          'stroke-dasharray': '3 6',
        })
      );
    }
  }

  for (const [from, to] of state.layerEdges || []) {
    const a = positions.get(from);
    const b = positions.get(to);
    if (!a || !b) {
      continue;
    }
    edgeLayer.appendChild(
      svgEl('line', {
        x1: a.x,
        y1: a.y,
        x2: b.x,
        y2: b.y,
        class: 'layer-edge',
      })
    );
  }

  const nodeLayer = svgEl('g');
  for (const [stop, point] of positions) {
    const depth = state.layerDepthByStop.get(stop) || 0;
    const color = DEPTH_COLORS[Math.min(depth, DEPTH_COLORS.length - 1)];
    const radius = depth === 0 ? 8 : positions.size > 600 ? 2.4 : 4.2;
    const node = svgEl('circle', {
      cx: point.x,
      cy: point.y,
      r: radius,
      class: 'layer-node',
      fill: depth === 0 ? color : '#081120',
      stroke: color,
      'stroke-width': depth === 0 ? 0 : 1.6,
    });
    node.addEventListener('mouseenter', makeTooltip(stop));
    node.addEventListener('mouseleave', hideTooltip);
    node.addEventListener('click', (event) => {
      event.stopPropagation();
      pickStop(stop);
    });
    nodeLayer.appendChild(node);
  }

  // Layer captions sit on the left of each ring, away from the destination
  // label at the top.
  const captionLayer = svgEl('g');
  const sourceTag = svgEl('text', {
    x: 0,
    y: 20,
    class: 'lay-caption',
    'text-anchor': 'middle',
  });
  sourceTag.textContent = 'source · 0 buses';
  captionLayer.appendChild(sourceTag);

  // Journey path on top. In the schematic ring view a straight leg reads
  // better than tracing every stop of the service.
  const journeyLayer = svgEl('g');
  state.journeyPaths = [];
  if (result && result.found) {
    for (const leg of result.legs) {
      const a = positions.get(leg.from);
      const b = positions.get(leg.to);
      if (!a || !b) {
        continue;
      }
      const color = colorFor(leg.route);
      journeyLayer.appendChild(
        svgEl('line', {
          x1: a.x,
          y1: a.y,
          x2: b.x,
          y2: b.y,
          class: 'journey-glow',
          stroke: color,
          'stroke-width': 13,
        })
      );
      const line = svgEl('line', {
        x1: a.x,
        y1: a.y,
        x2: b.x,
        y2: b.y,
        class: 'journey-line',
        stroke: color,
        'stroke-width': 3.6,
      });
      journeyLayer.appendChild(line);
      state.journeyPaths.push(line);
    }

    for (const stop of result.transfers) {
      const point = positions.get(stop);
      if (!point) {
        continue;
      }
      journeyLayer.appendChild(
        svgEl('circle', {
          cx: point.x,
          cy: point.y,
          r: 11,
          class: 'transfer-ring',
        })
      );
    }

    const destination = positions.get(result.destination);
    if (destination) {
      journeyLayer.appendChild(
        svgEl('circle', {
          cx: destination.x,
          cy: destination.y,
          r: 11,
          fill: 'none',
          stroke: '#fbbf24',
          'stroke-width': 1.5,
        })
      );
    }

    // Labels: endpoints and transfers only; the rest are in the tooltip.
    const labelStops = new Set([result.source, result.destination, ...result.transfers]);
    for (const stop of labelStops) {
      const point = positions.get(stop);
      if (!point) {
        continue;
      }
      const label = svgEl('text', {
        x: point.x,
        y: point.y - 16,
        class: 'stop-label strong',
        'text-anchor': 'middle',
      });
      label.textContent = stop;
      journeyLayer.appendChild(label);
    }
  }

  viewport.appendChild(depthLayer);
  viewport.appendChild(edgeLayer);
  viewport.appendChild(nodeLayer);
  viewport.appendChild(captionLayer);
  viewport.appendChild(journeyLayer);
}

function renderMap(options) {
  const opts = options || {};
  const svg = $('#map');
  const wrap = $('#mapWrap');
  const width = Math.max(320, wrap.clientWidth);
  const height = Math.max(240, wrap.clientHeight);

  cancelAnimationFrame(state.raf);
  clear(svg);
  state.routeLayer = null;
  state.routePaths = new Map();
  svg.setAttribute('viewBox', `0 0 ${width} ${height}`);

  const viewport = svgEl('g');
  state.viewport = viewport;
  svg.appendChild(viewport);

  const empty = $('#mapEmpty');
  if (!state.network) {
    empty.hidden = false;
    empty.textContent = 'No network loaded.';
    return;
  }
  empty.hidden = true;

  if (state.mode === 'layers') {
    layoutLayers();
    if (!state.layerPositions || state.layerPositions.size === 0) {
      empty.hidden = false;
      empty.textContent = 'Run a query to grow the BFS tree from the source.';
    } else {
      drawLayerMap(viewport);
    }
  } else {
    drawNetworkMap(viewport, width, height);
  }

  if (opts.fit) {
    fitView();
  }
  applyView();
  updateMapCaption();
  updateMapLegend();
  playJourney();
}

function updateMapLegend() {
  const host = $('#mapLegend');
  if (state.mode !== 'layers' || !state.result) {
    host.hidden = true;
    return;
  }
  host.hidden = false;
  clear(host);
  host.appendChild(elem('div', 'ml-title', 'one ring per bus'));
  const layers = state.result.stats.layers || [];
  for (const layer of layers.slice(0, 6)) {
    const row = elem('div', 'ml-row');
    const dot = elem('i');
    dot.style.color =
      DEPTH_COLORS[Math.min(layer.buses, DEPTH_COLORS.length - 1)];
    row.appendChild(dot);
    row.appendChild(
      elem('span', null, `${layer.buses} bus${layer.buses === 1 ? '' : 'es'}`)
    );
    row.appendChild(elem('b', null, formatCount(layer.stops)));
    host.appendChild(row);
  }
  if (layers.length > 6) {
    host.appendChild(elem('div', 'ml-row', `+${layers.length - 6} deeper layers`));
  }
  const foot = elem('div', 'ml-foot');
  const src = elem('span');
  src.appendChild(elem('i', 'lg-src'));
  src.appendChild(document.createTextNode('source'));
  const dst = elem('span');
  dst.appendChild(elem('i', 'lg-dst'));
  dst.appendChild(document.createTextNode('destination'));
  foot.appendChild(src);
  foot.appendChild(dst);
  host.appendChild(foot);
  if (state.result.stats.reachable_truncated) {
    host.appendChild(
      elem(
        'div',
        'ml-row',
        `showing ${formatCount(state.result.stats.reachable.length)} of ${formatCount(state.result.stats.stops_reached)}`
      )
    );
  }
}

function updateMapCaption() {
  const caption = $('#mapCaption');
  clear(caption);
  if (!state.network) {
    return;
  }
  if (state.mode === 'layers' && state.result) {
    caption.appendChild(elem('span', null, 'BFS from'));
    caption.appendChild(elem('b', null, state.result.source));
    caption.appendChild(
      elem('span', null, `· ${formatCount(state.result.stats.stops_reached)} stops settled`)
    );
    return;
  }
  caption.appendChild(elem('b', null, formatCount(state.stopSet.size)));
  caption.appendChild(elem('span', null, 'stops'));
  caption.appendChild(elem('b', null, formatCount(state.network.routes.length)));
  caption.appendChild(elem('span', null, 'routes'));
}

// ------------------------------------------------------------ journey play

function ease(t) {
  return t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2;
}

function playJourney() {
  const paths = state.journeyPaths;
  if (!paths || paths.length === 0 || !state.viewport) {
    return;
  }
  if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) {
    return;
  }

  const dot = svgEl('circle', { r: 4.2, class: 'traveller' });
  state.viewport.appendChild(dot);
  let legIndex = 0;
  let legStart = 0;
  let cancelled = false;
  state.raf = requestAnimationFrame(function step(now) {
    if (cancelled || legIndex >= paths.length) {
      dot.remove();
      return;
    }
    const path = paths[legIndex];
    const length = path.getTotalLength();
    if (!legStart) {
      legStart = now;
    }
    const duration = 620 + Math.min(900, length * 1.6);
    const t = Math.min(1, (now - legStart) / duration);
    const point = path.getPointAtLength(length * ease(t));
    dot.setAttribute('cx', point.x);
    dot.setAttribute('cy', point.y);
    if (t >= 1) {
      legIndex += 1;
      legStart = 0;
    }
    state.raf = requestAnimationFrame(step);
  });
}

// ----------------------------------------------------------- side panels

function showEmptyState(show) {
  $('#emptyState').hidden = !show;
}

function renderResult() {
  const host = $('#resultCard');
  const result = state.result;
  if (!result) {
    host.hidden = true;
    showEmptyState(true);
    return;
  }
  host.hidden = false;
  showEmptyState(false);
  clear(host);

  const card = elem('div', 'card result-card' + (result.found ? '' : ' no-route'));
  const head = elem('div', 'card-head');
  head.appendChild(
    elem('div', 'eyebrow', `Journey · ${result.source} → ${result.destination}`)
  );
  head.appendChild(elem('span', 'pill-time', formatMicros(result.stats.microseconds)));
  card.appendChild(head);

  if (result.found && result.buses === 0) {
    const big = elem('div', 'big-result');
    big.appendChild(elem('div', 'big-number zero', '0'));
    const label = elem('div', 'big-label');
    label.appendChild(elem('b', null, 'buses'));
    label.appendChild(elem('span', null, 'already there'));
    big.appendChild(label);
    card.appendChild(big);

    const note = elem('p', 'result-route');
    note.appendChild(document.createTextNode('You are already at '));
    note.appendChild(elem('b', null, result.destination));
    note.appendChild(document.createTextNode('. No bus is needed.'));
    card.appendChild(note);

    const foot = elem('div', 'card-foot');
    foot.appendChild(elem('span', 'micro', 'zero buses is trivially minimal'));
    const copy = elem('button', 'copy-btn', 'Copy CLI');
    copy.addEventListener('click', async () => {
      const ok = await copyText(cliText(result));
      toast(ok ? 'Copied exact CLI output' : 'Copy failed');
    });
    foot.appendChild(copy);
    card.appendChild(foot);
    host.appendChild(card);
    return;
  }

  if (!result.found) {
    card.appendChild(elem('div', 'no-route-title', 'No journey'));
    const unknown = [result.source, result.destination].filter(
      (stop) => !state.stopSet.has(stop)
    );
    const sub = elem('p', 'no-route-sub');
    if (unknown.length > 0) {
      sub.textContent =
        unknown.length === 1
          ? `"${unknown[0]}" is not served by any route in this network.`
          : 'Neither stop is served by any route in this network.';
    } else {
      sub.textContent =
        'No chain of open routes reaches the destination. The search exhausted every stop it could still reach.';
    }
    card.appendChild(sub);

    const grid = elem('div', 'stat-grid');
    const addStat = (value, label) => {
      const stat = elem('div', 'stat');
      stat.appendChild(elem('b', null, value));
      stat.appendChild(elem('span', null, label));
      grid.appendChild(stat);
    };
    addStat(formatCount(result.stats.stops_reached), 'stops reached');
    addStat(
      formatCount(result.stats.routes_expanded || result.stats.expanded_routes.length),
      'routes explored'
    );
    addStat(formatCount(state.closed.size), 'closures active');
    addStat(formatMicros(result.stats.microseconds), 'search time');
    card.appendChild(grid);

    if (state.closed.size > 0) {
      const note = elem('div', 'transfer-note');
      note.appendChild(elem('span', 'swap-glyph', '↺'));
      note.appendChild(
        elem('span', null, `${state.closed.size} closed route${state.closed.size === 1 ? '' : 's'} filtered; the index was not rebuilt.`)
      );
      card.appendChild(note);
    }

    const foot = elem('div', 'card-foot');
    if (state.closed.size > 0) {
      const reopen = elem('button', 'copy-btn', 'Reopen all');
      reopen.addEventListener('click', () => {
        state.closed.clear();
        renderRouteList();
        renderMap();
        runQuery();
      });
      foot.appendChild(reopen);
    } else {
      foot.appendChild(elem('span', 'micro', 'no route closures active'));
    }
    const copy = elem('button', 'copy-btn', 'Copy CLI');
    copy.addEventListener('click', async () => {
      const ok = await copyText(cliText(result));
      toast(ok ? 'Copied exact CLI output' : 'Copy failed');
    });
    foot.appendChild(copy);
    card.appendChild(foot);
    host.appendChild(card);
    return;
  }

  const big = elem('div', 'big-result');
  big.appendChild(elem('div', 'big-number', String(result.buses)));
  const label = elem('div', 'big-label');
  label.appendChild(elem('b', null, 'buses'));
  label.appendChild(elem('span', null, 'minimum boarding count'));
  big.appendChild(label);
  card.appendChild(big);

  const chain = elem('div', 'result-route');
  result.legs.forEach((leg, index) => {
    if (index > 0) {
      chain.appendChild(elem('span', 'arrow', '→'));
    }
    const pill = elem('span', 'route-pill');
    const dot = elem('span', 'swatch');
    dot.style.background = colorFor(leg.route);
    pill.appendChild(dot);
    pill.appendChild(document.createTextNode(leg.route));
    chain.appendChild(pill);
  });
  const servedStops = new Set();
  for (const leg of result.legs) {
    const route = state.routeByName.get(leg.route);
    if (route) {
      for (const stop of route.stops) {
        servedStops.add(stop);
      }
    }
  }
  chain.appendChild(
    elem('span', 'chain-meta', `· ${servedStops.size} stops on these routes`)
  );
  card.appendChild(chain);

  const legs = elem('div', 'legs');
  for (let index = 0; index < result.legs.length; index += 1) {
    const leg = result.legs[index];
    const row = elem('div', 'leg');
    const dot = elem('span', 'leg-dot');
    dot.style.setProperty('--c', colorFor(leg.route));
    const body = elem('div', 'leg-body');
    const top = elem('div', 'leg-top');
    top.appendChild(elem('span', 'leg-index', String(index + 1).padStart(2, '0')));
    const pill = elem('span', 'route-pill');
    const swatch = elem('span', 'swatch');
    swatch.style.background = colorFor(leg.route);
    pill.appendChild(swatch);
    pill.appendChild(document.createTextNode(leg.route));
    top.appendChild(pill);
    body.appendChild(top);

    const stops = elem('div', 'leg-stops');
    stops.appendChild(elem('span', null, leg.from));
    stops.appendChild(elem('span', 'arrow', '→'));
    const to = elem('span', 'to', leg.to);
    stops.appendChild(to);
    body.appendChild(stops);

    row.appendChild(dot);
    row.appendChild(body);
    legs.appendChild(row);
  }
  card.appendChild(legs);

  if (result.transfers.length > 0) {
    const note = elem('div', 'transfer-note');
    note.appendChild(elem('span', 'swap-glyph', '⇄'));
    const word = result.transfers.length === 1 ? 'Change' : 'Changes';
    note.appendChild(
      elem('span', null, `${word} at ${result.transfers.join(' · ')}`)
    );
    card.appendChild(note);
  }

  const foot = elem('div', 'card-foot');
  foot.appendChild(elem('span', 'micro', 'one index · no rebuilds'));
  const copy = elem('button', 'copy-btn', 'Copy CLI');
  copy.addEventListener('click', async () => {
    const ok = await copyText(cliText(result));
    toast(ok ? 'Copied exact CLI output' : 'Copy failed');
  });
  foot.appendChild(copy);
  card.appendChild(foot);
  host.appendChild(card);
}

function cliText(result) {
  if (!result.found) {
    return 'NO JOURNEY\n';
  }
  if (result.buses === 0) {
    return `JOURNEY 0 buses\n  already at ${result.destination}\n`;
  }
  let text = `JOURNEY ${result.buses} buses\n`;
  result.legs.forEach((leg, index) => {
    text += `  ${index + 1}: ${leg.route} ${leg.from} -> ${leg.to}\n`;
  });
  return text;
}

function renderProof() {
  const host = $('#proofCard');
  const result = state.result;
  if (!result) {
    host.hidden = true;
    return;
  }
  host.hidden = false;
  clear(host);

  const card = elem('div', 'card');
  const head = elem('div', 'card-head');
  head.appendChild(elem('div', 'eyebrow', 'Why this is minimal'));
  head.appendChild(
    elem('span', 'pill-time', `${result.stats.routes_expanded || result.stats.expanded_routes.length}/${state.network.routes.length} routes`)
  );
  card.appendChild(head);

  const layers = result.stats.layers || [];
  const maxStops = Math.max(1, ...layers.map((layer) => layer.stops));
  const chart = elem('div', 'layers');
  const shown = layers.slice(0, 10);
  for (const layer of shown) {
    const isDestination = result.found && layer.buses === result.buses;
    const row = elem('div', 'layer-row' + (isDestination ? ' dest' : ''));
    row.appendChild(elem('span', 'layer-label', `${layer.buses} bus${layer.buses === 1 ? '' : 'es'}`));
    const track = elem('div', 'layer-track');
    const fill = elem('div', 'layer-fill');
    fill.style.width = `${Math.max(2, (layer.stops / maxStops) * 100)}%`;
    track.appendChild(fill);
    row.appendChild(track);
    row.appendChild(elem('span', 'layer-count', String(layer.stops)));
    chart.appendChild(row);
  }
  card.appendChild(chart);

  if (layers.length > shown.length) {
    card.appendChild(
      elem('p', 'micro', `+${layers.length - shown.length} deeper layers not shown.`)
    );
  }

  const note = elem('p', 'proof-note');
  if (!result.found) {
    const deepest = Math.max(0, ...layers.map((layer) => layer.buses));
    if (!state.stopSet.has(result.source)) {
      note.appendChild(document.createTextNode('The source '));
      note.appendChild(elem('b', null, result.source));
      note.appendChild(
        document.createTextNode(' is not served by any route, so there is nothing to explore.')
      );
    } else {
      note.appendChild(document.createTextNode('BFS drained all '));
      note.appendChild(elem('b', null, formatCount(result.stats.stops_reached)));
      note.appendChild(document.createTextNode(' reachable stops without finding '));
      note.appendChild(elem('b', null, result.destination));
      note.appendChild(
        document.createTextNode(
          `. The active component ends at layer ${deepest}, so no open-route journey exists.`
        )
      );
    }
  } else if (result.buses === 0) {
    note.appendChild(document.createTextNode('Source equals destination. '));
    note.appendChild(elem('b', null, 'Zero'));
    note.appendChild(
      document.createTextNode(' is the smallest possible bus count, so the answer is trivially minimal.')
    );
  } else {
    note.appendChild(document.createTextNode('The destination first appears in layer '));
    note.appendChild(elem('b', null, String(result.buses)));
    note.appendChild(
      document.createTextNode(
        `. Layers 0–${result.buses - 1} hold every stop reachable with fewer buses, and it is in none of them — so no shorter journey can exist.`
      )
    );
  }
  card.appendChild(note);

  const grid = elem('div', 'stat-grid');
  const addStat = (value, label) => {
    const stat = elem('div', 'stat');
    stat.appendChild(elem('b', null, value));
    stat.appendChild(elem('span', null, label));
    grid.appendChild(stat);
  };
  addStat(formatCount(result.stats.stops_reached), 'stops settled');
  addStat(
    formatCount(result.stats.routes_expanded || result.stats.expanded_routes.length),
    'routes expanded'
  );
  addStat(`${result.stats.inactive_skipped}`, 'closed skipped');
  addStat(formatMicros(result.stats.microseconds), 'search time');
  card.appendChild(grid);

  host.appendChild(card);
}

function renderResilience() {
  const host = $('#resilienceCard');
  const result = state.result;
  if (!result || !state.network || state.network.routes.length < 2) {
    host.hidden = true;
    return;
  }
  host.hidden = false;
  clear(host);

  const card = elem('div', 'card');
  const head = elem('div', 'card-head');
  head.appendChild(elem('div', 'eyebrow', 'Closure sweep'));
  head.appendChild(
    elem('span', 'pill-time', `${state.network.routes.length} routes`)
  );
  card.appendChild(head);

  const top = elem('div', 'resilience-head');
  top.appendChild(
    elem(
      'p',
      null,
      'Close every route in turn and re-query. One index, many searches — the requirement made visible.'
    )
  );
  const button = elem('button', 'sweep-btn', 'Run sweep');
  top.appendChild(button);
  card.appendChild(top);

  const body = elem('div');
  card.appendChild(body);

  button.addEventListener('click', async () => {
    button.disabled = true;
    button.textContent = 'Sweeping…';
    try {
      const data = await api('/api/impact', {
        source: result.source,
        destination: result.destination,
        inactive: Array.from(state.closed),
      });
      state.impact = data;
      renderImpactRows(body, data);
    } catch (error) {
      body.appendChild(elem('p', 'field-error', error.message));
    } finally {
      button.disabled = false;
      button.textContent = 'Run sweep';
    }
  });

  if (state.impact) {
    renderImpactRows(body, state.impact);
  } else {
    body.appendChild(
      elem('p', 'micro', 'Answers which routes are critical for this pair — a transit planner question, not a graph exercise.')
    );
  }

  host.appendChild(card);
}

function renderImpactRows(host, data) {
  clear(host);

  const summary = elem('div', 'impact-summary');
  if (!data.baseline_found) {
    summary.appendChild(elem('span', 'impact-chip', 'no journey to protect'));
    summary.appendChild(
      elem('span', 'impact-chip', `${data.routes.length} re-queries`)
    );
    host.appendChild(summary);
  } else {
    const critical = elem(
      'span',
      'impact-chip bad',
      `${data.critical_count} critical route${data.critical_count === 1 ? '' : 's'}`
    );
    const rerouted = elem(
      'span',
      'impact-chip warn',
      `${data.rerouted_count} longer route${data.rerouted_count === 1 ? '' : 's'}`
    );
    const safe = elem(
      'span',
      'impact-chip',
      `${data.routes.length - data.critical_count - data.rerouted_count} unchanged`
    );
    summary.appendChild(critical);
    summary.appendChild(rerouted);
    summary.appendChild(safe);
    host.appendChild(summary);
  }

  if (data.routes.length === 0) {
    host.appendChild(elem('p', 'micro', 'Every route is already closed.'));
    return;
  }

  const rows = data.routes.slice().sort((a, b) => {
    if (a.delta !== b.delta) {
      return a.delta - b.delta;
    }
    return a.route.localeCompare(b.route);
  });

  const table = elem('div', 'impact-table');
  for (const row of rows) {
    const item = elem('div', 'impact-row');
    item.appendChild(elem('span', 'iroute', row.route));
    let stateLabel = 'safe';
    let stateClass = 'safe';
    if (!data.baseline_found) {
      stateLabel = 'no route';
      stateClass = 'safe';
    } else if (row.delta < 0) {
      stateLabel = 'stranded';
      stateClass = 'critical';
    } else if (row.delta > 0) {
      stateLabel = `+${row.delta} bus${row.delta === 1 ? '' : 'es'}`;
      stateClass = 'rerouted';
    }
    const stateNode = elem('span', `istate ${stateClass}`, stateLabel);
    item.appendChild(stateNode);
    item.appendChild(
      elem('span', 'idelta', row.delta > 0 ? `+${row.delta}` : row.delta < 0 ? '—' : '0')
    );
    table.appendChild(item);
  }
  host.appendChild(table);

  const foot = elem('p', 'micro');
  foot.style.marginTop = '9px';
  foot.textContent = `${data.routes.length} re-queries in ${formatMicros(data.microseconds)} · index built once`;
  host.appendChild(foot);
}

function renderLog() {
  const host = $('#logCard');
  if (state.log.length === 0) {
    host.hidden = true;
    return;
  }
  host.hidden = false;
  clear(host);

  const card = elem('div', 'card');
  const head = elem('div', 'card-head');
  head.appendChild(elem('div', 'eyebrow', 'Recent queries'));
  const clearBtn = elem('button', 'link', 'clear');
  clearBtn.addEventListener('click', () => {
    state.log = [];
    renderLog();
  });
  head.appendChild(clearBtn);
  card.appendChild(head);

  const list = elem('div', 'log-list');
  for (const entry of state.log) {
    const row = elem('button', 'log-row');
    const pair = elem('span', 'log-pair');
    pair.appendChild(document.createTextNode(entry.source));
    pair.appendChild(elem('span', 'arrow', '→'));
    pair.appendChild(document.createTextNode(entry.destination));
    row.appendChild(pair);
    row.appendChild(
      elem('span', 'log-result' + (entry.found ? '' : ' none'), entry.found ? `${entry.buses}` : 'none')
    );
    row.appendChild(elem('span', 'log-time', `${entry.microseconds}µs`));
    row.addEventListener('click', () => {
      $('#fromInput').value = entry.source;
      $('#toInput').value = entry.destination;
      state.closed = new Set(entry.inactive);
      renderRouteList();
      syncClearButtons();
      runQuery();
    });
    list.appendChild(row);
  }
  card.appendChild(list);
  host.appendChild(card);
}

// --------------------------------------------------------------- controls

function syncClearButtons() {
  document.querySelectorAll('[data-clear]').forEach((button) => {
    const field = button.getAttribute('data-clear');
    const input = field === 'from' ? $('#fromInput') : $('#toInput');
    button.hidden = input.value.trim() === '';
  });
}

function showFormError(message) {
  const node = $('#formError');
  node.textContent = message;
  node.hidden = false;
}

function hideFormError() {
  $('#formError').hidden = true;
}

function setBusy(busy) {
  state.busy = busy;
  const button = $('#findBtn');
  button.disabled = busy;
  clear(button);
  if (busy) {
    button.appendChild(document.createTextNode('Searching…'));
  } else {
    button.appendChild(document.createTextNode('Find journey '));
    button.appendChild(elem('kbd', null, '↵'));
  }
}

async function runQuery() {
  if (!state.network) {
    return;
  }
  if (state.busy) {
    // A closure toggled mid-flight must still be reflected; re-run once the
    // current request finishes instead of racing it.
    state.pendingQuery = true;
    return;
  }
  const source = $('#fromInput').value.trim();
  const destination = $('#toInput').value.trim();
  if (!source || !destination) {
    showFormError('Enter both a From and a To stop.');
    return;
  }
  hideFormError();
  const token = ++state.queryToken;
  setBusy(true);

  try {
    const result = await api('/api/journey', {
      source,
      destination,
      inactive: Array.from(state.closed),
    });
    if (token !== state.queryToken) {
      return;
    }
    state.result = result;
    state.impact = null;

    state.log.unshift({
      source: result.source,
      destination: result.destination,
      found: result.found,
      buses: result.buses,
      microseconds: result.stats.microseconds,
      inactive: Array.from(state.closed),
    });
    state.log = state.log.slice(0, 7);

    state.activeField = 'from';
    renderResult();
    renderProof();
    renderResilience();
    renderLog();
    updateHash();
    renderMap({ fit: state.mode === 'layers' });
  } catch (error) {
    showFormError(error.message);
  } finally {
    if (token === state.queryToken) {
      setBusy(false);
      if (state.pendingQuery) {
        state.pendingQuery = false;
        runQuery();
      }
    } else {
      $('#findBtn').disabled = false;
    }
  }
}

function toggleClosure(name) {
  if (state.closed.has(name)) {
    state.closed.delete(name);
  } else {
    state.closed.add(name);
  }
  renderRouteList();
  renderMap();
  if ($('#fromInput').value.trim() && $('#toInput').value.trim()) {
    runQuery();
  } else {
    updateHash();
  }
}

function applyHoveredRoute() {
  if (!state.routeLayer) {
    return;
  }
  const hovered = state.hoveredRoute;
  state.routeLayer.classList.toggle('has-hover', Boolean(hovered));
  for (const [name, path] of state.routePaths) {
    const active = name === hovered;
    path.classList.toggle('route-hover', active);
    if (active) {
      path.setAttribute('stroke', colorFor(name));
      // Raise above sibling paths so overlapping dense routes stay visible.
      if (path.parentNode === state.routeLayer) {
        state.routeLayer.appendChild(path);
      }
    } else if (path.dataset.baseStroke) {
      path.setAttribute('stroke', path.dataset.baseStroke);
    }
  }
}

function setHoveredRoute(routeName) {
  const next = routeName || null;
  if (state.hoveredRoute === next) {
    return;
  }
  state.hoveredRoute = next;
  applyHoveredRoute();
}

function renderRouteList() {
  const list = $('#routeList');
  clear(list);
  list.onmouseleave = () => setHoveredRoute(null);
  if (!state.network) {
    return;
  }
  for (const route of state.network.routes) {
    const row = elem('button', 'route-row');
    row.type = 'button';
    if (state.closed.has(route.name)) {
      row.classList.add('closed');
    }
    const swatch = elem('span', 'swatch');
    swatch.style.background = colorFor(route.name);
    row.appendChild(swatch);
    row.appendChild(elem('span', 'rname', route.name));
    row.appendChild(elem('span', 'rmeta', `${route.stops.length}`));
    row.title = state.closed.has(route.name)
      ? `Reopen ${route.name}`
      : `Close ${route.name} (${route.stops.length} stops)`;
    row.addEventListener('click', () => toggleClosure(route.name));
    row.addEventListener('mouseenter', () => setHoveredRoute(route.name));
    row.addEventListener('mouseleave', () => {
      if (state.hoveredRoute === route.name) {
        setHoveredRoute(null);
      }
    });
    row.addEventListener('focus', () => setHoveredRoute(route.name));
    row.addEventListener('blur', () => {
      if (state.hoveredRoute === route.name) {
        setHoveredRoute(null);
      }
    });
    list.appendChild(row);
  }
  const closedCount = $('#closedCount');
  closedCount.textContent =
    state.closed.size === 0 ? 'none closed' : `${state.closed.size} closed`;
  $('#clearClosed').style.visibility = state.closed.size > 0 ? 'visible' : 'hidden';
}

function updateNetworkPanels() {
  const name = state.network ? state.network.name : '—';
  const routes = state.network ? state.network.routes.length : 0;
  const stops = state.stopSet.size;
  let appearances = 0;
  if (state.network) {
    for (const route of state.network.routes) {
      appearances += route.stops.length;
    }
  }
  $('#chipName').textContent = name;
  $('#chipMeta').textContent = `${routes} routes · ${formatCount(stops)} stops · ${formatCount(appearances)} appearances`;
  $('#openNetwork').title = `Change network · ${name}`;

  const datalist = $('#stopList');
  clear(datalist);
  const sorted = state.stops.slice().sort((a, b) =>
    a.localeCompare(b, undefined, { numeric: true })
  );
  for (const stop of sorted) {
    const option = document.createElement('option');
    option.value = stop;
    datalist.appendChild(option);
  }
}

function renderQuickPairs() {
  const host = $('#quickPairs');
  clear(host);
  const pairs = state.queries.slice(0, 4);
  for (const pair of pairs) {
    const label =
      pair.inactive.length > 0
        ? `${pair.source} → ${pair.destination} −${pair.inactive.join(',')}`
        : `${pair.source} → ${pair.destination}`;
    const chip = elem('button', 'quick-pair', label);
    chip.addEventListener('click', () => {
      setPair(pair.source, pair.destination, pair.inactive);
      runQuery();
    });
    host.appendChild(chip);
  }
  const random = elem('button', 'quick-pair', 'random pair');
  random.addEventListener('click', () => {
    const stops = state.stops;
    if (stops.length < 2) {
      return;
    }
    const a = stops[Math.floor(Math.random() * stops.length)];
    let b = stops[Math.floor(Math.random() * stops.length)];
    if (b === a) {
      b = stops[(stops.indexOf(a) + 1) % stops.length];
    }
    setPair(a, b, []);
    runQuery();
  });
  host.appendChild(random);
}

function renderDemoPairs() {
  const host = $('#demoPairs');
  clear(host);
  for (const pair of state.queries.slice(0, 3)) {
    const button = elem('button', 'demo-pair');
    const left = elem('span', null, `${pair.source}`);
    const arrow = elem('span', 'dp-arrow', '→');
    const right = elem('span', null, `${pair.destination}`);
    const meta = elem(
      'span',
      'dp-route',
      pair.inactive.length > 0 ? `closed: ${pair.inactive.join(',')}` : ''
    );
    button.appendChild(left);
    button.appendChild(arrow);
    button.appendChild(right);
    button.appendChild(meta);
    button.addEventListener('click', () => {
      setPair(pair.source, pair.destination, pair.inactive);
      runQuery();
    });
    host.appendChild(button);
  }
}

function setPair(source, destination, inactive) {
  $('#fromInput').value = source;
  $('#toInput').value = destination;
  state.closed = new Set(inactive || []);
  renderRouteList();
  syncClearButtons();
}

// -------------------------------------------------------------- networking

async function installNetwork(name, routes, options) {
  const opts = options || {};
  const data = await api('/api/build', { name, routes });
  state.network = { name: data.name, routes: data.routes };
  state.queries = opts.queries || [];
  indexNetwork();
  layoutNetwork();
  state.closed = new Set();
  state.result = null;
  state.impact = null;
  state.log = [];
  state.activeField = 'from';
  state.sampleFile = opts.sampleFile || null;

  updateNetworkPanels();
  renderRouteList();
  renderQuickPairs();
  renderDemoPairs();
  renderResult();
  renderProof();
  renderResilience();
  renderLog();
  syncClearButtons();
  renderMap({ fit: true });
  updateHash();
}

async function loadSample(file, options) {
  const opts = options || {};
  const response = await fetch(`/samples/${file}`);
  if (!response.ok) {
    throw new Error(`Cannot load ${file}`);
  }
  const text = await response.text();
  const parsed = parseNetworkText(text);
  await installNetwork(file, parsed.routes, {
    queries: parsed.queries,
    sampleFile: file,
  });

  if (opts.auto !== false) {
    const pair = parsed.queries[0];
    if (pair) {
      setPair(pair.source, pair.destination, pair.inactive);
      await runQuery();
    }
  }
}

// ---------------------------------------------------------------- url hash

function updateHash() {
  const params = new URLSearchParams();
  if (state.sampleFile) {
    params.set('net', state.sampleFile);
  }
  const source = $('#fromInput').value.trim();
  const destination = $('#toInput').value.trim();
  if (source) {
    params.set('from', source);
  }
  if (destination) {
    params.set('to', destination);
  }
  if (state.closed.size > 0) {
    params.set('closed', Array.from(state.closed).join(','));
  }
  const hash = params.toString();
  if (hash) {
    history.replaceState(null, '', `#${hash}`);
  } else {
    history.replaceState(null, '', location.pathname);
  }
}

function readHash() {
  const params = new URLSearchParams(location.hash.replace(/^#/, ''));
  return {
    net: params.get('net'),
    from: params.get('from'),
    to: params.get('to'),
    closed: (params.get('closed') || '').split(',').filter(Boolean),
  };
}

// ------------------------------------------------------------- dialogs etc.

function renderSampleGrid() {
  const grid = $('#sampleGrid');
  clear(grid);
  for (const sample of SAMPLES) {
    const card = elem('button', 'sample-card');
    if (state.sampleFile === sample.file) {
      card.classList.add('on');
    }
    card.appendChild(elem('b', null, sample.title));
    card.appendChild(elem('span', null, sample.note));
    card.appendChild(elem('em', null, `${sample.file} · ${sample.badge}`));
    card.addEventListener('click', async () => {
      $('#networkDialog').close();
      try {
        await loadSample(sample.file);
        toast(`Loaded ${sample.title}`);
      } catch (error) {
        toast(error.message);
      }
    });
    grid.appendChild(card);
  }
}

function bindEvents() {
  $('#findBtn').addEventListener('click', () => runQuery());
  for (const id of ['#fromInput', '#toInput']) {
    const input = $(id);
    input.addEventListener('keydown', (event) => {
      if (event.key === 'Enter') {
        event.preventDefault();
        runQuery();
      }
    });
    input.addEventListener('input', () => {
      syncClearButtons();
      hideFormError();
    });
    input.addEventListener('focus', () => {
      state.activeField = id === '#fromInput' ? 'from' : 'to';
    });
  }

  document.querySelectorAll('[data-clear]').forEach((button) => {
    button.addEventListener('click', () => {
      const field = button.getAttribute('data-clear');
      const input = field === 'from' ? $('#fromInput') : $('#toInput');
      input.value = '';
      input.focus();
      state.activeField = field;
      syncClearButtons();
    });
  });

  $('#swapBtn').addEventListener('click', () => {
    const from = $('#fromInput').value;
    $('#fromInput').value = $('#toInput').value;
    $('#toInput').value = from;
    syncClearButtons();
    if ($('#fromInput').value && $('#toInput').value) {
      runQuery();
    }
  });

  $('#clearClosed').addEventListener('click', () => {
    state.closed.clear();
    renderRouteList();
    renderMap();
    if ($('#fromInput').value && $('#toInput').value) {
      runQuery();
    }
  });

  for (const button of document.querySelectorAll('.seg button')) {
    button.addEventListener('click', () => {
      const mode = button.getAttribute('data-mode');
      if (mode === state.mode) {
        return;
      }
      state.mode = mode;
      for (const other of document.querySelectorAll('.seg button')) {
        const active = other === button;
        other.classList.toggle('on', active);
        other.setAttribute('aria-selected', String(active));
      }
      renderMap({ fit: true });
    });
  }

  $('#zoomIn').addEventListener('click', () => zoomBy(1.25));
  $('#zoomOut').addEventListener('click', () => zoomBy(0.8));
  $('#zoomFit').addEventListener('click', () => fitView());

  const svg = $('#map');
  svg.addEventListener('wheel', (event) => {
    event.preventDefault();
    const rect = svg.getBoundingClientRect();
    const mx = event.clientX - rect.left;
    const my = event.clientY - rect.top;
    const factor = Math.exp(-event.deltaY * 0.0014);
    const next = Math.min(6, Math.max(0.15, state.view.k * factor));
    const ratio = next / state.view.k;
    state.view.x = mx - (mx - state.view.x) * ratio;
    state.view.y = my - (my - state.view.y) * ratio;
    state.view.k = next;
    applyView();
  }, { passive: false });

  svg.addEventListener('pointerdown', (event) => {
    if (event.button !== 0) {
      return;
    }
    state.dragged = false;
    state.drag = {
      x: event.clientX,
      y: event.clientY,
      vx: state.view.x,
      vy: state.view.y,
    };
    svg.setPointerCapture(event.pointerId);
    svg.classList.add('dragging');
  });

  svg.addEventListener('pointermove', (event) => {
    if (!state.drag) {
      return;
    }
    const dx = event.clientX - state.drag.x;
    const dy = event.clientY - state.drag.y;
    if (Math.abs(dx) + Math.abs(dy) > 4) {
      state.dragged = true;
    }
    state.view.x = state.drag.vx + dx;
    state.view.y = state.drag.vy + dy;
    applyView();
  });

  const endDrag = (event) => {
    if (!state.drag) {
      return;
    }
    state.drag = null;
    svg.classList.remove('dragging');
    if (event.pointerId !== undefined && svg.hasPointerCapture(event.pointerId)) {
      svg.releasePointerCapture(event.pointerId);
    }
  };
  svg.addEventListener('pointerup', endDrag);
  svg.addEventListener('pointercancel', endDrag);

  $('#openNetwork').addEventListener('click', () => {
    renderSampleGrid();
    $('#pasteError').hidden = true;
    $('#networkDialog').showModal();
  });
  $('#closeDialog').addEventListener('click', () => $('#networkDialog').close());

  $('#loadPaste').addEventListener('click', async () => {
    const text = $('#pasteInput').value;
    const errorNode = $('#pasteError');
    errorNode.hidden = true;
    try {
      const parsed = parseNetworkText(text);
      await installNetwork('pasted network', parsed.routes, {
        queries: parsed.queries,
      });
      $('#networkDialog').close();
      toast('Network built — index ready');
      const pair = parsed.queries[0];
      if (pair) {
        setPair(pair.source, pair.destination, pair.inactive);
        runQuery();
      }
    } catch (error) {
      errorNode.textContent = error.message;
      errorNode.hidden = false;
    }
  });

  $('#shareBtn').addEventListener('click', async () => {
    updateHash();
    const ok = await copyText(location.href);
    toast(ok ? 'Link copied' : 'Could not copy the link');
  });

  document.addEventListener('keydown', (event) => {
    const tag = (event.target.tagName || '').toLowerCase();
    const typing = tag === 'input' || tag === 'textarea';
    if (typing) {
      return;
    }
    if (event.key === 's' || event.key === 'S') {
      $('#swapBtn').click();
    } else if (event.key === '/') {
      event.preventDefault();
      $('#fromInput').focus();
      $('#fromInput').select();
    }
  });

  window.addEventListener('resize', debounce(() => {
    if (state.mode === 'network') {
      layoutNetwork();
    }
    renderMap({ fit: true });
  }, 180));
}

async function init() {
  bindEvents();
  syncClearButtons();

  const hash = readHash();
  const sample = SAMPLES.find((item) => item.file === hash.net) || SAMPLES[0];

  try {
    await loadSample(sample.file, { auto: false });
  } catch (error) {
    showFormError(`Could not load the starter network: ${error.message}`);
    return;
  }

  if (hash.from) {
    $('#fromInput').value = hash.from;
  }
  if (hash.to) {
    $('#toInput').value = hash.to;
  }

  let pair = null;
  let inactive = [];
  if (hash.from && hash.to) {
    pair = { source: hash.from, destination: hash.to };
    inactive = hash.closed;
  } else if (state.queries.length > 0) {
    pair = state.queries[0];
    inactive = pair.inactive || [];
  }

  if (pair) {
    setPair(pair.source, pair.destination, inactive);
  }
  syncClearButtons();

  if (pair) {
    await runQuery();
  } else {
    renderMap({ fit: true });
  }
}

document.addEventListener('DOMContentLoaded', init);
