import React, { useCallback, useRef } from 'react';
import { clamp } from '../engine/envelopeMath';

interface RotaryKnobProps {
  label: string;
  value: number;
  min: number;
  max: number;
  defaultValue: number;
  step?: number;
  skew?: number; // < 1 skews toward lower values (like JUCE NormalisableRange skew)
  accentColor: string;
  formatValue: (val: number) => string;
  onChange: (val: number) => void;
  title?: string;
}

export const RotaryKnob: React.FC<RotaryKnobProps> = ({
  label,
  value,
  min,
  max,
  defaultValue,
  step,
  skew = 1,
  accentColor,
  formatValue,
  onChange,
  title,
}) => {
  const dragRef = useRef<{ startY: number; startNorm: number } | null>(null);

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
  const startAngle = -135;
  const endAngle = 135;
  const angleDeg = startAngle + norm * (endAngle - startAngle);
  const angleRad = ((angleDeg - 90) * Math.PI) / 180;

  const size = 32;
  const cx = size / 2;
  const cy = size / 2;
  const r = 13;
  const innerR = 9.8;

  const polarToCartesian = (rad: number, radius: number) => ({
    x: cx + radius * Math.cos(rad),
    y: cy + radius * Math.sin(rad),
  });

  const describeArc = (a0Deg: number, a1Deg: number, radius: number) => {
    const r0 = ((a0Deg - 90) * Math.PI) / 180;
    const r1 = ((a1Deg - 90) * Math.PI) / 180;
    const p0 = polarToCartesian(r0, radius);
    const p1 = polarToCartesian(r1, radius);
    const largeArc = Math.abs(a1Deg - a0Deg) > 180 ? 1 : 0;
    return `M ${p0.x.toFixed(2)} ${p0.y.toFixed(2)} A ${radius} ${radius} 0 ${largeArc} 1 ${p1.x.toFixed(2)} ${p1.y.toFixed(2)}`;
  };

  const ptr = polarToCartesian(angleRad, innerR - 1.5);

  const handlePointerDown = (e: React.PointerEvent<HTMLDivElement>) => {
    if (e.button !== 0) return;
    e.preventDefault();
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
    dragRef.current = { startY: e.clientY, startNorm: norm };
  };

  const handlePointerMove = (e: React.PointerEvent<HTMLDivElement>) => {
    if (!dragRef.current) return;
    const sensitivity = e.shiftKey ? 450 : 150;
    const dy = dragRef.current.startY - e.clientY;
    const nextNorm = clamp(dragRef.current.startNorm + dy / sensitivity, 0, 1);
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

  return (
    <div
      className="flex flex-col items-center justify-center w-[52px] shrink-0 select-none"
      title={title}
    >
      <div className="text-[9.5px] font-bold leading-none text-[#c7c0df] mb-[2px] tracking-tight whitespace-nowrap">
        {label}
      </div>
      <div
        className="cursor-ns-resize touch-none"
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerUp}
        onDoubleClick={() => onChange(defaultValue)}
        onWheel={handleWheel}
      >
        <svg width={size} height={size} viewBox={`0 0 ${size} ${size}`}>
          <path
            d={describeArc(startAngle, endAngle, r)}
            fill="none"
            stroke="#514968"
            strokeWidth="2.2"
            strokeLinecap="round"
          />
          {norm > 0.004 && (
            <path
              d={describeArc(startAngle, angleDeg, r)}
              fill="none"
              stroke={accentColor}
              strokeWidth="2.2"
              strokeLinecap="round"
            />
          )}
          <circle
            cx={cx}
            cy={cy}
            r={innerR}
            fill="#1b1a24"
            stroke="#353346"
            strokeWidth="1"
          />
          <line
            x1={cx}
            y1={cy}
            x2={ptr.x}
            y2={ptr.y}
            stroke="#eceeff"
            strokeWidth="1.75"
            strokeLinecap="round"
          />
        </svg>
      </div>
      <div className="text-[10px] leading-none text-[#e4def4] mt-[1px] tabular-nums whitespace-nowrap">
        {formatValue(value)}
      </div>
    </div>
  );
};
