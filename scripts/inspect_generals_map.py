from __future__ import annotations

import argparse
import json
import re
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any


class ParseError(RuntimeError):
    pass


class Reader:
    def __init__(self, data: bytes | bytearray | memoryview, pos: int = 0, end: int | None = None, toc: dict[int, str] | None = None):
        self.data = data if isinstance(data, memoryview) else memoryview(data)
        self.pos = pos
        self.end = len(self.data) if end is None else end
        self.toc = toc or {}

    def remaining(self) -> int:
        return self.end - self.pos

    def tell(self) -> int:
        return self.pos

    def read(self, size: int) -> bytes:
        if self.pos + size > self.end:
            raise ParseError(f"read past end: pos={self.pos} size={size} end={self.end}")
        out = self.data[self.pos:self.pos + size].tobytes()
        self.pos += size
        return out

    def read_u8(self) -> int:
        return struct.unpack('<B', self.read(1))[0]

    def read_u16(self) -> int:
        return struct.unpack('<H', self.read(2))[0]

    def read_u32(self) -> int:
        return struct.unpack('<I', self.read(4))[0]

    def read_i32(self) -> int:
        return struct.unpack('<i', self.read(4))[0]

    def read_f32(self) -> float:
        return struct.unpack('<f', self.read(4))[0]

    def read_ascii(self) -> str:
        length = self.read_u16()
        if length == 0:
            return ""
        return self.read(length).decode('latin-1', errors='replace')

    def read_unicode(self) -> str:
        length = self.read_u16()
        if length == 0:
            return ""
        return self.read(length * 2).decode('utf-16le', errors='replace')

    def read_name_from_key(self) -> str:
        key_and_type = self.read_u32()
        toc_id = key_and_type >> 8
        return self.toc.get(toc_id, f"UNKNOWN_TOC_{toc_id}")

    def read_dict(self) -> dict[str, Any]:
        pair_count = self.read_u16()
        result: dict[str, Any] = {}
        for _ in range(pair_count):
            key_and_type = self.read_u32()
            data_type = key_and_type & 0xFF
            toc_id = key_and_type >> 8
            key_name = self.toc.get(toc_id, f"UNKNOWN_KEY_{toc_id}")
            if data_type == 0:
                value = bool(self.read_u8())
            elif data_type == 1:
                value = self.read_i32()
            elif data_type == 2:
                value = self.read_f32()
            elif data_type == 3:
                value = self.read_ascii()
            elif data_type == 4:
                value = self.read_unicode()
            else:
                raise ParseError(f"unknown dict datatype {data_type} for key {key_name}")
            result[key_name] = value
        return result

    def iter_chunks(self):
        while self.remaining() > 0:
            if self.remaining() < 10:
                raise ParseError(f"truncated chunk header at {self.pos}: remaining={self.remaining()}")
            chunk_id = self.read_u32()
            version = self.read_u16()
            size = self.read_i32()
            start = self.pos
            end = start + size
            if end > self.end:
                raise ParseError(f"chunk overruns parent: id={chunk_id} start={start} end={end} parent_end={self.end}")
            label = self.toc.get(chunk_id, f"UNKNOWN_CHUNK_{chunk_id}")
            child = Reader(self.data, start, end, self.toc)
            yield Chunk(label=label, version=version, size=size, reader=child)
            self.pos = end


@dataclass
class Chunk:
    label: str
    version: int
    size: int
    reader: Reader


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//.*", "", text)
    return text


def extract_enum_names(text: str, pattern: str) -> tuple[list[str], dict[str, int]]:
    match = re.search(pattern, text, flags=re.S)
    if not match:
        raise ParseError(f"could not find enum pattern: {pattern}")
    block = strip_comments(match.group(1))
    values: dict[str, int] = {}
    names: list[str] = []
    current = -1
    for part in block.split(','):
        item = part.strip()
        if not item:
            continue
        if '=' in item:
            name, rhs = [p.strip() for p in item.split('=', 1)]
            if re.fullmatch(r"-?(?:0x[0-9A-Fa-f]+|\d+)", rhs):
                current = int(rhs, 0)
            else:
                current = values.get(rhs, current + 1)
        else:
            name = item
            current += 1
        if current < 0:
            raise ParseError(f"invalid enum value while parsing {name}")
        while len(names) <= current:
            names.append(f"UNKNOWN_{len(names)}")
        names[current] = name
        values[name] = current
    return names, values


def load_enum_tables(root: Path) -> dict[str, Any]:
    scripts_h = (root / 'GeneralsMD/Code/GameEngine/Include/GameLogic/Scripts.h').read_text(encoding='latin-1')
    parameter_names, parameter_lookup = extract_enum_names(scripts_h, r"class\s+Parameter.*?enum\s+ParameterType\s*\{(.*?)\};")
    action_names, _ = extract_enum_names(scripts_h, r"class\s+ScriptAction.*?enum\s+ScriptActionType\s*\{(.*?)\};")
    condition_names, _ = extract_enum_names(scripts_h, r"class\s+Condition.*?enum\s+ConditionType\s*\{(.*?)\};")
    return {
        'parameter_names': parameter_names,
        'parameter_lookup': parameter_lookup,
        'action_names': action_names,
        'condition_names': condition_names,
    }


def read_toc(reader: Reader) -> dict[int, str]:
    tag = reader.read(4)
    if tag != b'CkMp':
        raise ParseError(f"expected CkMp table-of-contents header, got {tag!r}")
    count = reader.read_i32()
    toc: dict[int, str] = {}
    for _ in range(count):
        name_len = reader.read_u8()
        name = reader.read(name_len).decode('latin-1', errors='replace')
        toc_id = reader.read_u32()
        toc[toc_id] = name
    return toc


def refpack_decompress(comp: bytes, expected_size: int | None = None) -> bytes:
    s = 0

    def get_u8() -> int:
        nonlocal s
        if s >= len(comp):
            raise ParseError('unexpected end of refpack stream')
        value = comp[s]
        s += 1
        return value

    kind = (get_u8() << 8) | get_u8()
    if kind & 0x8000:
        if kind & 0x100:
            s += 4
        unpacked_size = int.from_bytes(comp[s:s + 4], 'big')
        s += 4
    else:
        if kind & 0x100:
            s += 3
        unpacked_size = int.from_bytes(comp[s:s + 3], 'big')
        s += 3

    out = bytearray()
    while True:
        first = get_u8()
        if not (first & 0x80):
            second = get_u8()
            run = first & 0x03
            out.extend(comp[s:s + run])
            s += run
            ref_pos = len(out) - 1 - (((first & 0x60) << 3) + second)
            run = ((first & 0x1C) >> 2) + 3
            for _ in range(run):
                out.append(out[ref_pos])
                ref_pos += 1
            continue
        if not (first & 0x40):
            second = get_u8()
            third = get_u8()
            run = second >> 6
            out.extend(comp[s:s + run])
            s += run
            ref_pos = len(out) - 1 - (((second & 0x3F) << 8) + third)
            run = (first & 0x3F) + 4
            for _ in range(run):
                out.append(out[ref_pos])
                ref_pos += 1
            continue
        if not (first & 0x20):
            second = get_u8()
            third = get_u8()
            fourth = get_u8()
            run = first & 0x03
            out.extend(comp[s:s + run])
            s += run
            ref_pos = len(out) - 1 - ((((first & 0x10) >> 4) << 16) + (second << 8) + third)
            run = (((first & 0x0C) >> 2) << 8) + fourth + 5
            for _ in range(run):
                out.append(out[ref_pos])
                ref_pos += 1
            continue
        run = ((first & 0x1F) << 2) + 4
        if run <= 112:
            out.extend(comp[s:s + run])
            s += run
            continue
        run = first & 0x03
        out.extend(comp[s:s + run])
        s += run
        break

    if expected_size is not None and len(out) != expected_size:
        raise ParseError(f"refpack size mismatch: expected {expected_size}, got {len(out)}")
    if len(out) != unpacked_size:
        raise ParseError(f"refpack header size mismatch: header says {unpacked_size}, got {len(out)}")
    return bytes(out)


def maybe_decompress(data: bytes) -> bytes:
    if len(data) >= 8 and data[:4] == b'EAR\x00':
        expected_size = struct.unpack_from('<I', data, 4)[0]
        return refpack_decompress(data[8:], expected_size)
    if len(data) >= 8 and data[:2] == b'ZL' and data[3] == 0:
        expected_size = struct.unpack_from('<I', data, 4)[0]
        out = zlib.decompress(data[8:])
        if len(out) != expected_size:
            raise ParseError(f"zlib size mismatch: expected {expected_size}, got {len(out)}")
        return out
    return data


def is_compressed_map_payload(data: bytes) -> bool:
    return (len(data) >= 8 and data[:4] == b'EAR\x00') or (len(data) >= 8 and data[:2] == b'ZL' and data[3] == 0)


def parse_parameter(reader: Reader, enum_tables: dict[str, Any]) -> dict[str, Any]:
    parameter_type = reader.read_i32()
    parameter_names: list[str] = enum_tables['parameter_names']
    parameter_lookup: dict[str, int] = enum_tables['parameter_lookup']
    parameter_name = parameter_names[parameter_type] if 0 <= parameter_type < len(parameter_names) else f'UNKNOWN_PARAMETER_{parameter_type}'
    if parameter_type == parameter_lookup['COORD3D']:
        value: dict[str, Any] = {
            'type_id': parameter_type,
            'type': parameter_name,
            'coord': {
                'x': reader.read_f32(),
                'y': reader.read_f32(),
                'z': reader.read_f32(),
            },
        }
    else:
        value = {
            'type_id': parameter_type,
            'type': parameter_name,
            'int': reader.read_i32(),
            'real': reader.read_f32(),
            'string': reader.read_ascii(),
        }
    return value


def parse_condition(chunk: Chunk, enum_tables: dict[str, Any]) -> dict[str, Any]:
    reader = chunk.reader
    condition_type_id = reader.read_i32()
    condition_names: list[str] = enum_tables['condition_names']
    condition_name = condition_names[condition_type_id] if 0 <= condition_type_id < len(condition_names) else f'UNKNOWN_CONDITION_{condition_type_id}'
    internal_name = reader.read_name_from_key() if chunk.version >= 4 else None
    param_count = reader.read_i32()
    params = [parse_parameter(reader, enum_tables) for _ in range(param_count)]
    return {
        'type_id': condition_type_id,
        'type': condition_name,
        'internal_name': internal_name,
        'parameters': params,
    }


def parse_or_condition(chunk: Chunk, enum_tables: dict[str, Any]) -> list[dict[str, Any]]:
    conditions: list[dict[str, Any]] = []
    for subchunk in chunk.reader.iter_chunks():
        if subchunk.label == 'Condition':
            conditions.append(parse_condition(subchunk, enum_tables))
    return conditions


def parse_action(chunk: Chunk, enum_tables: dict[str, Any]) -> dict[str, Any]:
    reader = chunk.reader
    action_type_id = reader.read_i32()
    action_names: list[str] = enum_tables['action_names']
    action_name = action_names[action_type_id] if 0 <= action_type_id < len(action_names) else f'UNKNOWN_ACTION_{action_type_id}'
    internal_name = reader.read_name_from_key() if chunk.version >= 2 else None
    param_count = reader.read_i32()
    params = [parse_parameter(reader, enum_tables) for _ in range(param_count)]
    return {
        'type_id': action_type_id,
        'type': action_name,
        'internal_name': internal_name,
        'parameters': params,
    }


def parse_script(chunk: Chunk, enum_tables: dict[str, Any]) -> dict[str, Any]:
    reader = chunk.reader
    script = {
        'name': reader.read_ascii(),
        'comment': reader.read_ascii(),
        'condition_comment': reader.read_ascii(),
        'action_comment': reader.read_ascii(),
        'active': bool(reader.read_u8()),
        'one_shot': bool(reader.read_u8()),
        'easy': bool(reader.read_u8()),
        'normal': bool(reader.read_u8()),
        'hard': bool(reader.read_u8()),
        'subroutine': bool(reader.read_u8()),
        'delay_evaluation_seconds': reader.read_i32() if chunk.version >= 2 else 0,
        'conditions': [],
        'actions': [],
        'false_actions': [],
    }
    for subchunk in reader.iter_chunks():
        if subchunk.label == 'OrCondition':
            script['conditions'].append(parse_or_condition(subchunk, enum_tables))
        elif subchunk.label == 'ScriptAction':
            script['actions'].append(parse_action(subchunk, enum_tables))
        elif subchunk.label == 'ScriptActionFalse':
            script['false_actions'].append(parse_action(subchunk, enum_tables))
    return script


def parse_script_group(chunk: Chunk, enum_tables: dict[str, Any]) -> dict[str, Any]:
    reader = chunk.reader
    group = {
        'name': reader.read_ascii(),
        'active': bool(reader.read_u8()),
        'subroutine': bool(reader.read_u8()) if chunk.version >= 2 else False,
        'scripts': [],
    }
    for subchunk in reader.iter_chunks():
        if subchunk.label == 'Script':
            group['scripts'].append(parse_script(subchunk, enum_tables))
    return group


def parse_script_list(chunk: Chunk, enum_tables: dict[str, Any]) -> dict[str, Any]:
    result = {
        'groups': [],
        'scripts': [],
    }
    for subchunk in chunk.reader.iter_chunks():
        if subchunk.label == 'ScriptGroup':
            result['groups'].append(parse_script_group(subchunk, enum_tables))
        elif subchunk.label == 'Script':
            result['scripts'].append(parse_script(subchunk, enum_tables))
    return result


def parse_player_scripts_list(chunk: Chunk, enum_tables: dict[str, Any]) -> list[dict[str, Any]]:
    lists: list[dict[str, Any]] = []
    for subchunk in chunk.reader.iter_chunks():
        if subchunk.label == 'ScriptList':
            lists.append(parse_script_list(subchunk, enum_tables))
    return lists


def parse_object(chunk: Chunk) -> dict[str, Any]:
    reader = chunk.reader
    x = reader.read_f32()
    y = reader.read_f32()
    z = reader.read_f32()
    if chunk.version <= 2:
        z = 0.0
    angle = reader.read_f32()
    flags = reader.read_i32()
    template_name = reader.read_ascii()
    properties = reader.read_dict() if chunk.version >= 2 else {}
    return {
        'template': template_name,
        'location': {'x': x, 'y': y, 'z': z},
        'angle': angle,
        'flags': flags,
        'properties': properties,
    }


def parse_objects_list(chunk: Chunk) -> list[dict[str, Any]]:
    objects: list[dict[str, Any]] = []
    for subchunk in chunk.reader.iter_chunks():
        if subchunk.label == 'Object':
            objects.append(parse_object(subchunk))
    return objects


def parse_sides_list(chunk: Chunk, enum_tables: dict[str, Any]) -> dict[str, Any]:
    reader = chunk.reader
    side_count = reader.read_i32()
    sides: list[dict[str, Any]] = []
    for _ in range(side_count):
        side = {
            'dict': reader.read_dict(),
            'build_list': [],
        }
        build_count = reader.read_i32()
        for _ in range(build_count):
            building_name = reader.read_ascii()
            template_name = reader.read_ascii()
            x = reader.read_f32()
            y = reader.read_f32()
            reader.read_f32()
            entry = {
                'building_name': building_name,
                'template_name': template_name,
                'location': {'x': x, 'y': y, 'z': 0.0},
                'angle': reader.read_f32(),
                'initially_built': bool(reader.read_u8()),
                'num_rebuilds': reader.read_i32(),
            }
            if chunk.version >= 3:
                entry.update({
                    'script': reader.read_ascii(),
                    'health': reader.read_i32(),
                    'whiner': bool(reader.read_u8()),
                    'unsellable': bool(reader.read_u8()),
                    'repairable': bool(reader.read_u8()),
                })
            side['build_list'].append(entry)
        sides.append(side)

    teams: list[dict[str, Any]] = []
    if chunk.version >= 2:
        team_count = reader.read_i32()
        for _ in range(team_count):
            teams.append(reader.read_dict())

    script_lists: list[dict[str, Any]] = []
    for subchunk in reader.iter_chunks():
        if subchunk.label == 'PlayerScriptsList':
            script_lists = parse_player_scripts_list(subchunk, enum_tables)

    for side, script_list in zip(sides, script_lists):
        side['scripts'] = script_list
    for side in sides[len(script_lists):]:
        side['scripts'] = {'groups': [], 'scripts': []}

    return {
        'sides': sides,
        'teams': teams,
    }


def parse_map_bytes(raw: bytes, enum_tables: dict[str, Any], source_path: str = '<memory>') -> dict[str, Any]:
    data = maybe_decompress(raw)
    reader = Reader(data)
    toc = read_toc(reader)
    reader.toc = toc
    result: dict[str, Any] = {
        'source_path': source_path,
        'compressed': is_compressed_map_payload(raw),
        'top_level_chunks': [],
        'world_info': {},
        'objects': [],
        'sides': [],
        'teams': [],
    }

    for chunk in reader.iter_chunks():
        result['top_level_chunks'].append({'label': chunk.label, 'version': chunk.version, 'size': chunk.size})
        if chunk.label == 'WorldInfo':
            result['world_info'] = chunk.reader.read_dict()
        elif chunk.label == 'ObjectsList':
            result['objects'] = parse_objects_list(chunk)
        elif chunk.label == 'SidesList':
            sides_payload = parse_sides_list(chunk, enum_tables)
            result['sides'] = sides_payload['sides']
            result['teams'] = sides_payload['teams']
    return result


def parse_map(path: Path, enum_tables: dict[str, Any]) -> dict[str, Any]:
    raw = path.read_bytes()
    return parse_map_bytes(raw, enum_tables, source_path=str(path))


def format_param_value(param: dict[str, Any]) -> str:
    if 'coord' in param:
        coord = param['coord']
        return f"{param['type']}(coord=({coord['x']:.2f},{coord['y']:.2f},{coord['z']:.2f}))"
    return f"{param['type']}(int={param['int']}, real={param['real']:.3f}, string={param['string']!r})"


def write_text_summary(payload: dict[str, Any], out_path: Path) -> None:
    lines: list[str] = []
    lines.append(f"Source Map: {payload['source_path']}")
    lines.append(f"Compressed: {payload['compressed']}")
    lines.append('')
    lines.append('Top-level chunks:')
    for chunk in payload['top_level_chunks']:
        lines.append(f"  - {chunk['label']} v{chunk['version']} size={chunk['size']}")

    lines.append('')
    lines.append('WorldInfo:')
    for key, value in sorted(payload['world_info'].items()):
        lines.append(f"  {key} = {value!r}")

    lines.append('')
    lines.append('Objects:')
    for obj in payload['objects']:
        props = obj['properties']
        named = props.get('objectName', '')
        script = props.get('objectScriptAttachment', '')
        owner_team = props.get('originalOwner', '')
        health = props.get('objectInitialHealth', '')
        lines.append(
            f"  template={obj['template']} named={named!r} owner_team={owner_team!r} script={script!r} health={health!r} "
            f"location=({obj['location']['x']:.1f},{obj['location']['y']:.1f},{obj['location']['z']:.1f}) angle={obj['angle']:.3f}"
        )

    lines.append('')
    lines.append('Sides and build lists:')
    for index, side in enumerate(payload['sides']):
        side_dict = side['dict']
        player_name = side_dict.get('playerName', '(neutral)') or '(neutral)'
        lines.append(
            f"SIDE index={index} player={player_name!r} faction={side_dict.get('playerFaction', '')!r} allies={side_dict.get('playerAllies', '')!r} enemies={side_dict.get('playerEnemies', '')!r}"
        )
        for build in side['build_list']:
            lines.append(
                f"  BUILD template={build['template_name']} name={build['building_name']} script={build.get('script', '')!r} "
                f"initially_built={build['initially_built']} rebuilds={build['num_rebuilds']} health={build.get('health', '')!r} "
                f"location=({build['location']['x']:.1f},{build['location']['y']:.1f},{build['location']['z']:.1f}) angle={build['angle']:.3f}"
            )

    lines.append('')
    lines.append('Teams:')
    for team in payload['teams']:
        lines.append(
            f"TEAM name={team.get('teamName', '')!r} owner={team.get('teamOwner', '')!r} home={team.get('teamHome', '')!r} "
            f"condition={team.get('teamProductionCondition', '')!r} priority={team.get('teamProductionPriority', '')!r}"
        )
        lines.append(
            '  scripts=' + json.dumps({
                'onCreate': team.get('teamOnCreateScript', ''),
                'onIdle': team.get('teamOnIdleScript', ''),
                'onDestroyed': team.get('teamOnDestroyedScript', ''),
                'onEnemySighted': team.get('teamEnemySightedScript', ''),
                'onAllClear': team.get('teamAllClearScript', ''),
            }, ensure_ascii=False)
        )

    lines.append('')
    lines.append('Scripts:')
    for side in payload['sides']:
        side_dict = side['dict']
        player_name = side_dict.get('playerName', '(neutral)') or '(neutral)'
        lines.append(f"PLAYER {player_name!r}")
        script_list = side.get('scripts', {'groups': [], 'scripts': []})
        for group in script_list.get('groups', []):
            lines.append(f"  GROUP {group['name']!r} active={group['active']} subroutine={group['subroutine']}")
            for script in group['scripts']:
                lines.extend(format_script_lines(script, indent='    '))
        for script in script_list.get('scripts', []):
            lines.extend(format_script_lines(script, indent='  '))

    out_path.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def format_script_lines(script: dict[str, Any], indent: str) -> list[str]:
    lines = [
        f"{indent}SCRIPT {script['name']!r} active={script['active']} one_shot={script['one_shot']} subroutine={script['subroutine']} "
        f"easy={script['easy']} normal={script['normal']} hard={script['hard']} delay={script['delay_evaluation_seconds']}"
    ]
    if script['comment']:
        lines.append(f"{indent}  COMMENT {script['comment']!r}")
    if script['condition_comment']:
        lines.append(f"{indent}  CONDITION_COMMENT {script['condition_comment']!r}")
    if script['action_comment']:
        lines.append(f"{indent}  ACTION_COMMENT {script['action_comment']!r}")
    for or_index, or_group in enumerate(script['conditions'], start=1):
        lines.append(f"{indent}  OR_GROUP {or_index}")
        for condition in or_group:
            params = ', '.join(format_param_value(param) for param in condition['parameters'])
            lines.append(
                f"{indent}    CONDITION {condition['type']} internal={condition['internal_name']!r} params=[{params}]"
            )
    for action in script['actions']:
        params = ', '.join(format_param_value(param) for param in action['parameters'])
        lines.append(f"{indent}  ACTION {action['type']} internal={action['internal_name']!r} params=[{params}]")
    for action in script['false_actions']:
        params = ', '.join(format_param_value(param) for param in action['parameters'])
        lines.append(f"{indent}  ELSE_ACTION {action['type']} internal={action['internal_name']!r} params=[{params}]")
    return lines


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description='Dump Generals/Zero Hour .map mission data without using game executables.')
    parser.add_argument('map_path', type=Path)
    parser.add_argument('--json-out', type=Path)
    parser.add_argument('--text-out', type=Path)
    parser.add_argument('--workspace-root', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args(argv)

    map_path = args.map_path.resolve()
    json_out = args.json_out or map_path.with_suffix(map_path.suffix + '.json')
    text_out = args.text_out or map_path.with_suffix(map_path.suffix + '.txt')

    enum_tables = load_enum_tables(args.workspace_root.resolve())
    payload = parse_map(map_path, enum_tables)

    json_out.parent.mkdir(parents=True, exist_ok=True)
    text_out.parent.mkdir(parents=True, exist_ok=True)
    json_out.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding='utf-8')
    write_text_summary(payload, text_out)

    print(f'Wrote {json_out}')
    print(f'Wrote {text_out}')
    print(f"Sides: {len(payload['sides'])}, Teams: {len(payload['teams'])}, Objects: {len(payload['objects'])}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
