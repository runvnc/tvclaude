import type { Register } from 'claude-code'

// Inside tvclaude, draw the person's own prompts Turbo-style: black on teal.
// A theme can color the bar behind them but not their text separately, so
// the colors are theme keys that tvclaude's theme sets (promptBorder: teal,
// inverseText: black in the classic theme); they follow Options > Theme.
const BAR = 'promptBorder'
const TEXT = 'inverseText'

export const register: Register = on => {
  on('ui.render', { component: 'UserMessage' }, async ($, e, next) => {
    if (e.surface !== 'terminal' || e.props.origin.kind !== 'composer') {
      return next(e)
    }
    if ((await $.env.get('TURBOVISION')) !== '1') {
      return next(e)
    }
    const { Box, Text } = $.ui.resolve(e)

    return (
      <Box backgroundColor={BAR} paddingX={1}>
        <Text color={TEXT} backgroundColor={BAR}>{'> ' + e.props.text}</Text>
      </Box>
    )
  })
}
