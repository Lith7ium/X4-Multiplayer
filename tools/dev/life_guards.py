"""Keep generated economy/bubble scripts valid after either original ship dies."""
import xml.etree.ElementTree as E


def _write_only_changes(cue):
    """Wrap hull/shield writes so unchanged values are not re-applied on every packet.

    Each set_object_shield counts as a change and re-shows the HUD shield bar, so the
    guest's unconditional writes made the bar flash. Differences up to 0.5 points are skipped.
    """
    parents = {child: parent for parent in cue.iter() for child in parent}
    for node in list(cue.iter()):
        prop = {'set_object_hull': 'hullpercentage', 'set_object_shield': 'shieldpercentage'}.get(node.tag)
        parent = parents.get(node)
        if not prop or parent is None or parent.get('value', '').endswith(' gt 0.25'):
            continue
        obj, value = node.get('object'), node.get('exact')
        guard = E.Element('do_if', value='(%s.%s - %s) * (%s.%s - %s) gt 0.25' % (obj, prop, value, obj, prop, value))
        index = list(parent).index(node)
        parent.remove(node)
        guard.append(node)
        parent.insert(index, guard)


def guard_bubble(xml):
    root = E.fromstring(xml)
    actions = root.find('./cues/cue/actions')
    actions.insert(0, E.Element('set_value', name='$dead', exact='[false,false]'))
    actions.insert(1, E.Element('set_value', name='$remoteBalance', exact='100000Cr'))
    initial = actions.find("do_all[@counter='$i']")
    initial.insert(1, E.Element('set_object_min_hull', object='$ship', exact='1'))
    for node in list(root.find(".//cue[@name='Publish']/actions").iter()) + list(root.find(".//cue[@name='Readback']/actions").iter()):
        for attribute, expression in list(node.attrib.items()):
            for slot in (1, 2):
                ship = '$ships.{' + str(slot) + '}'
                for prop in ('hullpercentage', 'shieldpercentage', 'cargo.{ware.energycells}.count'):
                    value = ship + '.' + prop
                    if prop.endswith('percentage') and node.tag == 'raise_lua_event':
                        # Pilot health (health_game.lua): the last applied pilot value of a live ship.
                        k = '1' if prop == 'hullpercentage' else '2'
                        value = '(if @$healthLive.{%d} then $healthValue.{%d}.{%s} else %s)' % (slot, slot, k, value)
                    expression = expression.replace(ship + '.' + prop,
                        '(if ' + ship + '.isoperational and not $dead.{' + str(slot) + '} then ' + value + ' else 0)')
                expression = expression.replace(ship + '.dock or', ship + '.isoperational and ' + ship + '.dock or')
                expression = expression.replace('if ' + ship + '.dock then', 'if ' + ship + '.isoperational and ' + ship + '.dock then')
            node.set(attribute, expression)
    loop = root.find(".//cue[@name='Apply']/actions/do_all")
    gate = E.Element('do_if', value='$ship.isoperational and not Arena.$dead.{$i}')
    selected = list(loop)[1:6] # hull, shield, cargo delta and its two branches
    # Pilot health: the guest never writes health or dead of the ship it pilots from WORLD.
    health = E.Element('do_if', value='not @Arena.$healthOwn.{$i}')
    for child in selected[:2]:
        loop.remove(child); health.append(child)
    gate.append(health)
    _write_only_changes(root.find(".//cue[@name='Apply']"))
    for child in selected[2:]:
        loop.remove(child); gate.append(child)
    loop.insert(1, E.fromstring('''<do_if value="$v.{($i - 1) * 3 + 1} le (if @Arena.$healthOwn.{$i} then 0 else 1.01)"><set_value name="Arena.$dead.{$i}" exact="true"/></do_if>'''))
    loop.insert(2, gate)
    for node in loop.findall('do_if'):
        if node.get('value', '').startswith('$v.{13 + $i}'):
            node.set('value', '$ship.isoperational and not Arena.$dead.{$i} and ' + node.get('value'))
    command = root.find(".//cue[@name='Command']/actions")
    busy = command.find("do_if[@value='Arena.$busy']")
    busy.set('value', 'Arena.$busy or not $ship.isoperational or Arena.$dead.{$slot}')
    return E.tostring(root, encoding='unicode')


def guard_trade(xml):
    root = E.fromstring(xml)
    setup = root.find('./cues/cue/actions')
    setup.append(E.Element('set_object_account', object='$arena.$donor'))
    setup.append(E.Element('set_object_min_hull', object='$arena.$donor', exact='100'))
    tick = root.find(".//cue[@name='Tick']/actions")
    tick.insert(0, E.fromstring('''<do_if value="$arena.$proxy.isoperational and not $arena.$dead.{3 - $arena.$localSlot}"><set_value name="$arena.$remoteBalance" exact="$arena.$proxy.money"/></do_if>'''))
    for node in list(tick.iter()) + list(root.find(".//cue[@name='Readback']/actions").iter()):
        for attribute, expression in list(node.attrib.items()):
            expression = expression.replace('$ships.{2}.money', '$arena.$remoteBalance')
            expression = expression.replace('$arena.$proxy.money', '$arena.$remoteBalance') if node.get('name') != '$arena.$remoteBalance' else expression
            for slot in (1, 2):
                ship = '$ships.{' + str(slot) + '}'
                expression = expression.replace(ship + '.cargo.{$ware}.count',
                    '(if ' + ship + '.isoperational and not $arena.$dead.{' + str(slot) + '} then ' + ship + '.cargo.{$ware}.count else 0)')
                expression = expression.replace(ship + '.cargo.{ware.energycells}.count',
                    '(if ' + ship + '.isoperational then ' + ship + '.cargo.{ware.energycells}.count else 0)')
            expression = expression.replace('$arena.$local.dock and', '$arena.$local.isoperational and $arena.$local.dock and')
            node.set(attribute, expression)
    remote = root.find(".//cue[@name='RemoteDock']/actions")
    gate = E.Element('do_if', value='$ship.isoperational and not Market.$arena.$dead.{3 - Market.$arena.$localSlot}')
    for node in list(remote)[2:]:
        remote.remove(node); gate.append(node)
    remote.append(gate)
    apply = root.find(".//cue[@name='Apply']/actions")
    for loop in list(apply.iter('do_all')):
        children = list(loop)
        if children and children[0].get('name') in ('$account', '$object'):
            obj = children[0].get('name')
            gate = E.Element('do_if', value=obj + '.isoperational')
            for node in children[1:]:
                loop.remove(node); gate.append(node)
            loop.append(gate)
    order = root.find(".//cue[@name='Order']/actions")
    nodes = list(order)
    index = next(i for i, n in enumerate(nodes) if n.get('name') == '$station') + 1
    gate = E.Element('do_if', value='$ship.isoperational and not Market.$arena.$dead.{$slot}')
    for node in nodes[index:]:
        order.remove(node); gate.append(node)
    order.append(gate)
    order.append(E.fromstring('''<do_else><raise_lua_event name="'X4LAN_Trade.Result'" param="'%s|REJECTED'.[$args.$request]"/></do_else>'''))
    return E.tostring(root, encoding='unicode')
