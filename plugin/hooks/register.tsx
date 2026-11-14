import type { Register } from 'claude-code'

// Inside tvclaude, draw the person's own prompts Turbo-style: black on teal.
// A theme can color the bar behind them but not their text separately.
const TEAL = '#00AAAA'
const BLACK = '#000000'

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
      <Box backgroundColor={TEAL} paddingX={1}>
        <Text color={BLACK} backgroundColor={TEAL}>{'> ' + e.props.text}</Text>
      </Box>
    )
  })
}
