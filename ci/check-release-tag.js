// Validate publication identity before building and again before uploading.
module.exports = async function ({ github, context, tag }) {
    if (!/^v2\.[0-9]+\.[0-9]+(?:\.[0-9]+)?(?:-mainnet|-rc\.[1-9][0-9]*)?$/.test(tag)) {
        throw new Error('Invalid V2 release tag');
    }
    if (!/^[0-9a-f]{40}$/.test(context.sha)) throw new Error('A full source commit is required');
    let release;
    try {
        release = (await github.rest.repos.getReleaseByTag({ ...context.repo, tag })).data;
    } catch (error) {
        if (error.status !== 404) throw error;
    }
    if (release) throw new Error('A release already exists; refusing to replace it');
    let ref;
    try {
        ref = (await github.rest.git.getRef({ ...context.repo, ref: `tags/${tag}` })).data;
    } catch (error) {
        if (error.status !== 404) throw error;
    }
    if (!ref) return;
    let object = ref.object;
    const seen = new Set();
    for (let depth = 0; object.type === 'tag' && depth < 8; depth++) {
        if (seen.has(object.sha)) throw new Error('Cyclic tag reference');
        seen.add(object.sha);
        object = (await github.rest.git.getTag({ ...context.repo, tag_sha: object.sha })).data.object;
    }
    if (object.type !== 'commit' || object.sha !== context.sha) {
        throw new Error('Existing Git tag refers to another source commit; choose a new tag');
    }
};
