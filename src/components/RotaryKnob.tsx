import React, { useRef } from 'react';
import { clamp } from '../audio/curves';

interface RotaryKnobProps {
  label: string;
  valueText: string;
  normValue: number; // 0..1
  defaultNorm?: number;
  bipolar?: boolean;
  accentColour?: string;
  size?: number;
  onChangeNorm: (nextNorm: number) => void;
}

export const RotaryKnob: React.FC<RotaryKnobProps> = ({
  label,
  valueText,
  normValue,
  defaultNorm = 0.5,
  bipolar = false,
  accentColour = '#7d92ff',
  size = 26,
  onChangeNorm,
}) => {
  const dragRef = useRef<{ startY: number; startNorm: number } | null>(null);
  const clamped = clamp(normValue, 0, 1);

  const startDeg = -135;
  const endDeg = 135;
  const angleDeg = startDeg + clamped * (endDeg - startDeg);
  const angleRad = ((angleDeg - 90) * Math.PI) / 180;

  const r = size * 0.41;
  const cx = size * 0.5;
  const cy = size * 0.5;

  const arcPath = (fromDeg: number, toDeg: number) => {
    const a1 = ((fromDeg - 90) * Math.PI) / 180;
    const a2 = ((toDeg - 90) * Math.PI) / 180;
    const x1 = cx + r * Math.cos(a1);
    const y1 = cy + r * Math.sin(a1);
    const x2 = cx + r * Math.cos(a2);
    const y2 = cy + r * Math.sin(a2);
    const largeArc = Math.abs(toDeg - fromDeg) > 180 ? 1 : 0;
    const sweep = toDeg >= fromDeg ? 1 : 0;
    return `M ${x1.toFixed(2)} ${y1.toFixed(2)} A ${r.toFixed(2)} ${r.toFixed(2)} 0 ${largeArc} ${sweep} ${x2.toFixed(2)} ${y2.toFixed(2)}`;
  };

  const handlePointerDown = (e: React.PointerEvent<SVGSVGElement>) => {
    if (e.button !== 0) return;
    dragRef.current = { startY: e.clientY, startNorm: clamped };
    e.currentTarget.setPointerCapture(e.pointerId);
  };

  const handlePointerMove = (e: React.PointerEvent<SVGSVGElement>) => {
    if (!dragRef.current) return;
    const delta = (dragRef.current.startY - e.clientY) / (e.shiftKey ? 280 : 120);
    onChangeNorm(clamp(dragRef.current.startNorm + delta, 0, 1));
  };

  const handlePointerUp = (e: React.PointerEvent<SVGSVGElement>) => {
    if (dragRef.current) {
      dragRef.current = null;
      if (e.currentTarget.hasPointerCapture(e.pointerId)) {
        e.currentTarget.releasePointerCapture(e.pointerId);
      }
    }
  };

  const handleDoubleClick = () => {
    onChangeNorm(clamp(defaultNorm, 0, 1));
  };

  const handleKeyDown = (e: React.KeyboardEvent<SVGSVGElement>) => {
    const step = e.shiftKey ? 0.01 : 0.04;
    if (e.key === 'ArrowUp' || e.key === 'ArrowRight') {
      e.preventDefault();
      onChangeNorm(clamp(clamped + step, 0, 1));
    } else if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') {
      e.preventDefault();
      onChangeNorm(clamp(clamped - step, 0, 1));
    } else if (e.key === 'Home') {
      e.preventDefault();
      onChangeNorm(0);
    } else if (e.key === 'End') {
      e.preventDefault();
      onChangeNorm(1);
    }
  };

  const ptrX = cx + (r - 2.2) * Math.cos(angleRad);
  const ptrY = cy + (r - 2.2) * Math.sin(angleRad);
  const zeroDeg = bipolar ? 0 : startDeg;

  return (
    <div
      className="flex flex-col items-center select-none min-w-[42px]"
      title={`${label}: ${valueText} (drag vertically, double-click to reset)`}
    >
      <span className="text-[9.5px] font-medium text-[#8f92ab] leading-tight mb-0.5 whitespace-nowrap">
        {label}
      </span>
      <svg
        role="slider"
        tabIndex={0}
        aria-label={label}
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={Math.round(clamped * 100)}
        aria-valuetext={valueText}
        width={size}
        height={size}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerUp}
        onPointerCancel={handlePointerUp}
        onDoubleClick={handleDoubleClick}
        onKeyDown={handleKeyDown}
        className="cursor-ns-resize touch-none focus:outline-none focus-visible:ring-1 focus-visible:ring-[#7d92ff] rounded-full"
      >
        <path
          d={arcPath(startDeg, endDeg)}
          fill="none"
          stroke="#2d2b3b"
          strokeWidth={2.2}
          strokeLinecap="round"
        />
        {Math.abs(angleDeg - zeroDeg) > 0.5 && (
          <path
            d={arcPath(
              Math.min(zeroDeg, angleDeg),
              Math.max(zeroDeg, angleDeg)
            )}
            fill="none"
            stroke={accentColour}
            strokeWidth={2.2}
            strokeLinecap="round"
          />
        )}
        <circle
          cx={cx}
          cy={cy}
          r={r - 3.2}
          fill="#1b1a24"
          stroke="#353346"
          strokeWidth={1}
        />
        <line
          x1={cx}
          y1={cy}
          x2={ptrX}
          y2={ptrY}
          stroke="#eceeff"
          strokeWidth={1.75}
          strokeLinecap="round"
        />
      </svg>
      <span className="text-[9.5px] font-mono tabular-nums text-[#d6d9ef] leading-tight mt-0.5 whitespace-nowrap">
        {valueText}
      </span>
    </div>
  );
};
