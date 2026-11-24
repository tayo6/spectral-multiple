import React, { useCallback, useRef } from 'react';
import { clamp } from '../engine/envelopeMath';

interface SnapbackSlideCardProps {
  label: string;
  value: number;
  min: number;
  max: number;
  defaultValue: number;
  step?: number;
  skew?: number;
  bipolar?: boolean;
  accentColor: string;
  formatValue: (val: number) => string;
  onChange: (val: number) => void;
  title?: string;
}

export const SnapbackSlideCard: React.FC<SnapbackSlideCardProps> = ({
  label,
  value,
  min,
  max,
  defaultValue,
  step,
  skew = 1,
  bipolar = false,
  accentColor,
  formatValue,
  onChange,
  title,
}) => {
  const dragRef = useRef<{ startX: number; startY: number; startNorm: number } | null>(null);

  const valueToNorm = useCallback(
    (v: number) => {
      const linear = clamp((v - min) / Math.max(1e-9, max - min), 0, 1);
      return skew !== 1 ? Math.pow(linear, skew) : linear;
    },
    [min, max, skew]
  );

  const normToValue = useCallback(
    (norm: number) => {
      const clamped = clamp(norm, 0, 1);
      const unskewed = skew !== 1 ? Math.pow(clamped, 1 / skew) : clamped;
      let v = min + unskewed * (max - min);
      if (step && step > 0) {
        v = Math.round(v / step) * step;
      }
      return clamp(v, min, max);
    },
    [min, max, skew, step]
  );

  const norm = valueToNorm(value);
  const centerNorm = bipolar ? valueToNorm(0) : 0;

  const handlePointerDown = (e: React.PointerEvent<HTMLDivElement>) => {
    if (e.button !== 0) return;
    e.preventDefault();
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
    dragRef.current = { startX: e.clientX, startY: e.clientY, startNorm: norm };
  };

  const handlePointerMove = (e: React.PointerEvent<HTMLDivElement>) => {
    if (!dragRef.current) return;
    const sensitivity = e.shiftKey ? 420 : 140;
    const dx = e.clientX - dragRef.current.startX;
    const dy = dragRef.current.startY - e.clientY;
    const delta = Math.abs(dx) >= Math.abs(dy) ? dx : dy;
    const nextNorm = clamp(dragRef.current.startNorm + delta / sensitivity, 0, 1);
    onChange(normToValue(nextNorm));
  };

  const handlePointerUp = (e: React.PointerEvent<HTMLDivElement>) => {
    if (!dragRef.current) return;
    dragRef.current = null;
    try {
      (e.currentTarget as HTMLElement).releasePointerCapture(e.pointerId);
    } catch {
      // ignore
    }
  };

  const handleWheel = (e: React.WheelEvent<HTMLDivElement>) => {
    e.preventDefault();
    const delta = e.deltaY < 0 ? 0.035 : -0.035;
    onChange(normToValue(norm + (e.shiftKey ? delta * 0.25 : delta)));
  };

  const fillLeftPct = bipolar ? Math.min(centerNorm, norm) * 100 : 0;
  const fillWidthPct = bipolar
    ? Math.abs(norm - centerNorm) * 100
    : norm * 100;

  return (
    <div
      title={title}
      onPointerDown={handlePointerDown}
      onPointerMove={handlePointerMove}
      onPointerUp={handlePointerUp}
      onDoubleClick={() => onChange(defaultValue)}
      onWheel={handleWheel}
      className="h-[44px] bg-[#1f1e28] hover:bg-[#252432] border border-[#2f2d3e] hover:border-[#3e3b52] rounded-[3px] px-2 py-1 flex flex-col justify-between select-none cursor-ew-resize touch-none transition-colors"
    >
      <div className="flex items-center justify-between gap-1 leading-none">
        <span className="text-[9.5px] font-medium text-[#9592aa] truncate">
          {label}
        </span>
      </div>

      <div className="text-[11px] font-semibold text-[#f1eff9] leading-none tabular-nums truncate">
        {formatValue(value)}
      </div>

      {/* Linear Snapback track + white dot thumb */}
      <div className="relative h-[6px] flex items-center">
        <div className="w-full h-[2px] rounded-full bg-[#2e2c3e] relative overflow-hidden">
          <div
            className="absolute top-0 bottom-0 rounded-full"
            style={{
              left: `${fillLeftPct}%`,
              width: `${fillWidthPct}%`,
              backgroundColor: accentColor,
            }}
          />
        </div>
        <div
          className="absolute w-[6px] h-[6px] rounded-full bg-[#ffffff] -translate-x-1/2 pointer-events-none"
          style={{
            left: `${norm * 100}%`,
            boxShadow: '0 0 4px rgba(255,255,255,0.45)',
          }}
        />
      </div>
    </div>
  );
};

interface SnapbackVerticalFaderProps {
  label: string;
  value: number;
  min: number;
  max: number;
  defaultValue: number;
  step?: number;
  accentColor: string;
  formatValue: (val: number) => string;
  onChange: (val: number) => void;
  title?: string;
}

export const SnapbackVerticalFader: React.FC<SnapbackVerticalFaderProps> = ({
  label,
  value,
  min,
  max,
  defaultValue,
  step = 0.001,
  accentColor,
  formatValue,
  onChange,
  title,
}) => {
  const trackRef = useRef<HTMLDivElement>(null);
  const draggingRef = useRef(false);

  const norm = clamp((value - min) / Math.max(1e-9, max - min), 0, 1);

  const updateFromClientY = (clientY: number) => {
    const rect = trackRef.current?.getBoundingClientRect();
    if (!rect || rect.height <= 1) return;
    const rawNorm = clamp((rect.bottom - clientY) / rect.height, 0, 1);
    let next = min + rawNorm * (max - min);
    if (step > 0) next = Math.round(next / step) * step;
    onChange(clamp(next, min, max));
  };

  return (
    <div
      title={title}
      onDoubleClick={() => onChange(defaultValue)}
      onWheel={(e) => {
        e.preventDefault();
        const d = (e.deltaY < 0 ? 0.035 : -0.035) * (e.shiftKey ? 0.25 : 1);
        let next = min + clamp(norm + d, 0, 1) * (max - min);
        if (step > 0) next = Math.round(next / step) * step;
        onChange(clamp(next, min, max));
      }}
      className="h-full bg-[#1f1e28] hover:bg-[#252432] border border-[#2f2d3e] hover:border-[#3e3b52] rounded-[3px] px-1.5 py-1.5 flex flex-col items-center justify-between select-none transition-colors"
    >
      <span className="text-[9.5px] font-medium text-[#9592aa] leading-none">
        {label}
      </span>

      <div
        ref={trackRef}
        onPointerDown={(e) => {
          if (e.button !== 0) return;
          e.preventDefault();
          (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
          draggingRef.current = true;
          updateFromClientY(e.clientY);
        }}
        onPointerMove={(e) => {
          if (!draggingRef.current) return;
          updateFromClientY(e.clientY);
        }}
        onPointerUp={(e) => {
          draggingRef.current = false;
          try {
            (e.currentTarget as HTMLElement).releasePointerCapture(e.pointerId);
          } catch {
            // ignore
          }
        }}
        className="relative w-full flex-1 my-1.5 flex justify-center cursor-ns-resize touch-none"
      >
        <div className="w-[2px] h-full rounded-full bg-[#2e2c3e] relative">
          <div
            className="absolute left-0 right-0 bottom-0 rounded-full"
            style={{
              height: `${norm * 100}%`,
              backgroundColor: accentColor,
            }}
          />
          <div
            className="absolute left-1/2 -translate-x-1/2 translate-y-1/2 w-[7px] h-[7px] rounded-full bg-[#ffffff] pointer-events-none"
            style={{
              bottom: `${norm * 100}%`,
              boxShadow: '0 0 5px rgba(255,255,255,0.55)',
            }}
          />
        </div>
      </div>

      <span className="text-[10.5px] font-semibold text-[#f1eff9] leading-none tabular-nums whitespace-nowrap">
        {formatValue(value)}
      </span>
    </div>
  );
};

interface SnapbackDualFilterSliderProps {
  highPassHz: number;
  lowPassHz: number;
  accentColor: string;
  onChangeHp: (hz: number) => void;
  onChangeLp: (hz: number) => void;
}

export const SnapbackDualFilterSlider: React.FC<SnapbackDualFilterSliderProps> = ({
  highPassHz,
  lowPassHz,
  accentColor,
  onChangeHp,
  onChangeLp,
}) => {
  const trackRef = useRef<HTMLDivElement>(null);
  const activeThumbRef = useRef<'hp' | 'lp' | null>(null);

  const formatHz = (v: number) =>
    v >= 1000 ? `${(v / 1000).toFixed(1)}k` : `${Math.round(v)}`;

  // Left half (0..0.5): HP 20..1000 Hz (skew 0.35)
  // Right half (0.5..1.0): LP 1000..20000 Hz (skew 0.35)
  const hpNormHalf = Math.pow(clamp((highPassHz - 20) / (1000 - 20), 0, 1), 0.35);
  const lpNormHalf = Math.pow(clamp((lowPassHz - 1000) / (20000 - 1000), 0, 1), 0.35);

  const hpPos = hpNormHalf * 0.5;
  const lpPos = 0.5 + lpNormHalf * 0.5;

  const updateFromClientX = (clientX: number, thumb: 'hp' | 'lp') => {
    const rect = trackRef.current?.getBoundingClientRect();
    if (!rect || rect.width <= 1) return;
    const xNorm = clamp((clientX - rect.left) / rect.width, 0, 1);
    if (thumb === 'hp') {
      const u = clamp(xNorm / 0.5, 0, 1);
      const hz = Math.round(20 + Math.pow(u, 1 / 0.35) * (1000 - 20));
      onChangeHp(clamp(hz, 20, 1000));
    } else {
      const u = clamp((xNorm - 0.5) / 0.5, 0, 1);
      const hz = Math.round((1000 + Math.pow(u, 1 / 0.35) * (20000 - 1000)) / 10) * 10;
      onChangeLp(clamp(hz, 1000, 20000));
    }
  };

  return (
    <div
      title="HP (20 Hz - 1 kHz) / LP (1 kHz - 20 kHz) passband slider — double-click left/right to reset"
      className="h-[44px] bg-[#1f1e28] border border-[#2f2d3e] rounded-[3px] px-2.5 py-1.5 flex flex-col justify-between select-none"
    >
      <div className="flex items-center justify-between text-[9.5px] leading-none">
        <div
          className="flex items-center gap-1.5 cursor-pointer"
          onDoubleClick={() => onChangeHp(20)}
        >
          <span className="font-medium text-[#9592aa]">HP</span>
          <span className="text-[#d9d6eb] font-semibold tabular-nums">
            {formatHz(highPassHz)}
          </span>
        </div>
        <div
          className="flex items-center gap-1.5 cursor-pointer"
          onDoubleClick={() => onChangeLp(20000)}
        >
          <span className="text-[#d9d6eb] font-semibold tabular-nums">
            {formatHz(lowPassHz)}
          </span>
          <span className="font-medium text-[#9592aa]">LP</span>
        </div>
      </div>

      <div
        ref={trackRef}
        onPointerDown={(e) => {
          if (e.button !== 0) return;
          const rect = trackRef.current?.getBoundingClientRect();
          if (!rect) return;
          const xNorm = clamp((e.clientX - rect.left) / rect.width, 0, 1);
          const thumb: 'hp' | 'lp' =
            Math.abs(xNorm - hpPos) <= Math.abs(xNorm - lpPos) ? 'hp' : 'lp';
          activeThumbRef.current = thumb;
          (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
          updateFromClientX(e.clientX, thumb);
        }}
        onPointerMove={(e) => {
          if (!activeThumbRef.current) return;
          updateFromClientX(e.clientX, activeThumbRef.current);
        }}
        onPointerUp={(e) => {
          activeThumbRef.current = null;
          try {
            (e.currentTarget as HTMLElement).releasePointerCapture(e.pointerId);
          } catch {
            // ignore
          }
        }}
        onDoubleClick={(e) => {
          const rect = trackRef.current?.getBoundingClientRect();
          if (!rect) return;
          const xNorm = (e.clientX - rect.left) / rect.width;
          if (xNorm < 0.5) onChangeHp(20);
          else onChangeLp(20000);
        }}
        className="relative h-[12px] flex items-center cursor-ew-resize touch-none"
      >
        <div className="w-full h-[2px] rounded-full bg-[#2e2c3e] relative">
          <div
            className="absolute top-0 bottom-0 rounded-full"
            style={{
              left: `${hpPos * 100}%`,
              width: `${Math.max(0, (lpPos - hpPos) * 100)}%`,
              backgroundColor: accentColor,
            }}
          />
        </div>
        <div
          className="absolute w-[7px] h-[7px] rounded-full bg-[#ffffff] -translate-x-1/2 pointer-events-none"
          style={{
            left: `${hpPos * 100}%`,
            boxShadow: '0 0 4px rgba(255,255,255,0.55)',
          }}
        />
        <div
          className="absolute w-[7px] h-[7px] rounded-full bg-[#ffffff] -translate-x-1/2 pointer-events-none"
          style={{
            left: `${lpPos * 100}%`,
            boxShadow: '0 0 4px rgba(255,255,255,0.55)',
          }}
        />
      </div>
    </div>
  );
};
