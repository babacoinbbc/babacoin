const assert = require('assert').strict;
const check = require('../../ci/check-release-tag.js');
module.exports = async function () {
    const sha = '1'.repeat(40), old = '2'.repeat(40);
    const absent = () => { throw Object.assign(new Error('Not found'), { status: 404 }); };
    function args(overrides = {}) {
        return { tag: 'v2.0.1', context: { sha, repo: { owner: 'test', repo: 'test' } },
            github: { rest: { repos: { getReleaseByTag: overrides.release || absent },
                git: { getRef: overrides.ref || absent, getTag: overrides.tag || absent } } } };
    }
    await check(args());
    await check(args({ ref: () => ({ data: { object: { type: 'commit', sha } } }) }));
    await check(args({ ref: () => ({ data: { object: { type: 'tag', sha: old } } }),
        tag: () => ({ data: { object: { type: 'commit', sha } } }) }));
    await assert.rejects(() => check(args({ release: () => ({ data: { draft: true } }) })), /already exists/);
    await assert.rejects(() => check(args({ ref: () => ({ data: { object: { type: 'commit', sha: old } } }) })), /another source/);
    await assert.rejects(() => check(args({ ref: () => ({ data: { object: { type: 'tag', sha: old } } }),
        tag: () => ({ data: { object: { type: 'commit', sha: old } } }) })), /another source/);
    await assert.rejects(() => check(args({ ref: () => { throw Object.assign(new Error('Forbidden'), { status: 403 }); } })), /Forbidden/);
    await assert.rejects(() => check({ ...args(), tag: 'v2.0.1/invalid' }), /Invalid/);
    return { result: 'PASS', cases: 8 };
};
if (require.main === module) module.exports().then(result => console.log(JSON.stringify(result))).catch(error => {
    console.error(error);
    process.exitCode = 1;
});
